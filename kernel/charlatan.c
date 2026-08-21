// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "../include/uapi/charlatan.h"

#define CHARLATAN_DEVICE_NAME "charlatan0"
#define CHARLATAN_QUEUE_CAPACITY 128U
#define CHARLATAN_MAX_READ_EVENTS 64U
#define CHARLATAN_MAX_PERIOD_MS 60000U
#define CHARLATAN_MAX_OVERFLOW_INJECT 100000U
#define CHARLATAN_MAX_CONTROL_DELAY_MS 1000U

struct charlatan_reader {
	u64 reset_generation;
};

struct charlatan_device {
	struct mutex lock;
	struct mutex producer_control_lock;
	wait_queue_head_t read_wait;
	struct delayed_work producer_work;
	struct charlatan_event queue[CHARLATAN_QUEUE_CAPACITY];
	u32 head;
	u32 tail;
	u32 depth;
	u32 period_ms;
	u32 fail_next_reads;
	u32 fail_next_writes;
	u32 producer_control_delay_ms;
	bool producer_running;
	bool producer_control_delay_active;
	bool shutting_down;
	atomic_t producer_control_waiters;
	u64 reset_generation;
	u64 next_sequence;
	struct charlatan_stats stats;
};

static dev_t charlatan_devt;
static struct cdev charlatan_cdev;
static struct class *charlatan_class;
static struct charlatan_device charlatan;

static void charlatan_control_delay(u32 delay_ms)
{
	if (!delay_ms)
		return;

	mutex_lock(&charlatan.lock);
	charlatan.producer_control_delay_active = true;
	mutex_unlock(&charlatan.lock);
	msleep(delay_ms);
	mutex_lock(&charlatan.lock);
	charlatan.producer_control_delay_active = false;
	mutex_unlock(&charlatan.lock);
}

static bool charlatan_has_events_or_reset(struct charlatan_reader *reader)
{
	bool ready;

	mutex_lock(&charlatan.lock);
	ready = charlatan.depth != 0 || charlatan.reset_generation != reader->reset_generation ||
		charlatan.shutting_down;
	mutex_unlock(&charlatan.lock);
	return ready;
}

static bool charlatan_enqueue_locked(u32 value, u32 flags, bool injected)
{
	struct charlatan_event *event;

	if (charlatan.depth == CHARLATAN_QUEUE_CAPACITY) {
		charlatan.stats.dropped++;
		return false;
	}

	event = &charlatan.queue[charlatan.head];
	event->sequence = charlatan.next_sequence++;
	event->produced_ns = ktime_get_ns();
	event->value = value;
	event->flags = flags;
	charlatan.head = (charlatan.head + 1) % CHARLATAN_QUEUE_CAPACITY;
	charlatan.depth++;
	charlatan.stats.produced++;
	if (injected)
		charlatan.stats.injected++;
	charlatan.stats.queue_depth = charlatan.depth;
	return true;
}

static void charlatan_producer(struct work_struct *work)
{
	bool wake = false;
	u32 delay_ms = 0;

	mutex_lock(&charlatan.lock);
	if (!charlatan.shutting_down && charlatan.producer_running) {
		wake = charlatan_enqueue_locked((u32)charlatan.next_sequence, 0, false);
		delay_ms = charlatan.period_ms;
	}
	mutex_unlock(&charlatan.lock);

	if (wake)
		wake_up_interruptible(&charlatan.read_wait);
	if (delay_ms)
		mod_delayed_work(system_wq, &charlatan.producer_work,
				 msecs_to_jiffies(delay_ms));
}

static int charlatan_open(struct inode *inode, struct file *file)
{
	struct charlatan_reader *reader;

	reader = kzalloc(sizeof(*reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;

	mutex_lock(&charlatan.lock);
	if (charlatan.shutting_down) {
		mutex_unlock(&charlatan.lock);
		kfree(reader);
		return -ENODEV;
	}
	reader->reset_generation = charlatan.reset_generation;
	mutex_unlock(&charlatan.lock);
	file->private_data = reader;
	return 0;
}

static int charlatan_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t charlatan_read(struct file *file, char __user *buffer, size_t count,
			      loff_t *offset)
{
	struct charlatan_reader *reader = file->private_data;
	size_t requested_events;
	size_t copied = 0;
	bool made_space = false;

	if (count < sizeof(struct charlatan_event) ||
	    count % sizeof(struct charlatan_event) != 0)
		return -EINVAL;
	requested_events = min_t(size_t, count / sizeof(struct charlatan_event),
				 CHARLATAN_MAX_READ_EVENTS);

	for (;;) {
		mutex_lock(&charlatan.lock);
		if (charlatan.fail_next_reads) {
			charlatan.fail_next_reads--;
			charlatan.stats.read_failures++;
			mutex_unlock(&charlatan.lock);
			return -EIO;
		}
		if (charlatan.reset_generation != reader->reset_generation) {
			reader->reset_generation = charlatan.reset_generation;
			mutex_unlock(&charlatan.lock);
			return -ECANCELED;
		}
		if (charlatan.depth)
			break;
		if (charlatan.shutting_down) {
			mutex_unlock(&charlatan.lock);
			return -ENODEV;
		}
		mutex_unlock(&charlatan.lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(charlatan.read_wait,
				charlatan_has_events_or_reset(reader)))
			return -ERESTARTSYS;
	}

	while (copied < requested_events && charlatan.depth) {
		struct charlatan_event event = charlatan.queue[charlatan.tail];

		if (copy_to_user(buffer + copied * sizeof(event), &event, sizeof(event))) {
			mutex_unlock(&charlatan.lock);
			return copied ? copied * sizeof(event) : -EFAULT;
		}
		charlatan.tail = (charlatan.tail + 1) % CHARLATAN_QUEUE_CAPACITY;
		charlatan.depth--;
		charlatan.stats.consumed++;
		charlatan.stats.queue_depth = charlatan.depth;
		made_space = true;
		copied++;
	}
	mutex_unlock(&charlatan.lock);
	if (made_space)
		wake_up_interruptible(&charlatan.read_wait);
	return copied * sizeof(struct charlatan_event);
}

static ssize_t charlatan_write(struct file *file, const char __user *buffer, size_t count,
			       loff_t *offset)
{
	struct charlatan_event input;
	bool wake;

	if (count != sizeof(input))
		return -EINVAL;
	if (copy_from_user(&input, buffer, sizeof(input)))
		return -EFAULT;

	mutex_lock(&charlatan.lock);
	if (charlatan.fail_next_writes) {
		charlatan.fail_next_writes--;
		charlatan.stats.write_failures++;
		mutex_unlock(&charlatan.lock);
		return -EIO;
	}
	wake = charlatan_enqueue_locked(input.value, input.flags, true);
	mutex_unlock(&charlatan.lock);
	if (!wake)
		return -ENOSPC;
	wake_up_interruptible(&charlatan.read_wait);
	return sizeof(input);
}

static __poll_t charlatan_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &charlatan.read_wait, wait);
	mutex_lock(&charlatan.lock);
	if (charlatan.depth)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (charlatan.depth < CHARLATAN_QUEUE_CAPACITY)
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (charlatan.reset_generation != ((struct charlatan_reader *)file->private_data)->reset_generation)
		mask |= EPOLLPRI;
	if (charlatan.shutting_down)
		mask |= EPOLLHUP;
	mutex_unlock(&charlatan.lock);
	return mask;
}

static void charlatan_reset_locked(void)
{
	charlatan.stats.reset_discards += charlatan.depth;
	charlatan.depth = 0;
	charlatan.head = 0;
	charlatan.tail = 0;
	charlatan.stats.queue_depth = 0;
	charlatan.stats.resets++;
	charlatan.reset_generation++;
}

static long charlatan_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
	struct charlatan_rate_config rate;
	struct charlatan_fault_config faults;
	struct charlatan_stats stats;
	u32 overflow;
	u32 index;
	u32 resume_period;
	u32 control_delay_ms;
	bool wake = false;

	switch (command) {
	case CHARLATAN_IOC_RESET:
		mutex_lock(&charlatan.lock);
		charlatan_reset_locked();
		mutex_unlock(&charlatan.lock);
		wake_up_interruptible(&charlatan.read_wait);
		return 0;
	case CHARLATAN_IOC_GET_STATS:
		mutex_lock(&charlatan.lock);
		stats = charlatan.stats;
		stats.abi_version = CHARLATAN_ABI_VERSION;
		stats.queue_capacity = CHARLATAN_QUEUE_CAPACITY;
		stats.producer_period_ms = charlatan.period_ms;
		stats.producer_running = charlatan.producer_running;
		stats.producer_control_waiters = atomic_read(&charlatan.producer_control_waiters);
		stats.producer_control_delay_active = charlatan.producer_control_delay_active;
		mutex_unlock(&charlatan.lock);
		return copy_to_user((void __user *)argument, &stats, sizeof(stats)) ? -EFAULT : 0;
	case CHARLATAN_IOC_SET_RATE:
		if (copy_from_user(&rate, (void __user *)argument, sizeof(rate)))
			return -EFAULT;
		if (rate.abi_version != CHARLATAN_ABI_VERSION || rate.period_ms > CHARLATAN_MAX_PERIOD_MS)
			return -EINVAL;
		atomic_inc(&charlatan.producer_control_waiters);
		mutex_lock(&charlatan.producer_control_lock);
		mutex_lock(&charlatan.lock);
		charlatan.period_ms = rate.period_ms;
		charlatan.producer_running = rate.period_ms != 0;
		control_delay_ms = charlatan.producer_control_delay_ms;
		mutex_unlock(&charlatan.lock);
		charlatan_control_delay(control_delay_ms);
		cancel_delayed_work_sync(&charlatan.producer_work);
		if (rate.period_ms)
			mod_delayed_work(system_wq, &charlatan.producer_work,
					 msecs_to_jiffies(rate.period_ms));
		mutex_unlock(&charlatan.producer_control_lock);
		atomic_dec(&charlatan.producer_control_waiters);
		return 0;
	case CHARLATAN_IOC_PAUSE:
		atomic_inc(&charlatan.producer_control_waiters);
		mutex_lock(&charlatan.producer_control_lock);
		mutex_lock(&charlatan.lock);
		charlatan.producer_running = false;
		control_delay_ms = charlatan.producer_control_delay_ms;
		mutex_unlock(&charlatan.lock);
		charlatan_control_delay(control_delay_ms);
		cancel_delayed_work_sync(&charlatan.producer_work);
		mutex_unlock(&charlatan.producer_control_lock);
		atomic_dec(&charlatan.producer_control_waiters);
		return 0;
	case CHARLATAN_IOC_RESUME:
		atomic_inc(&charlatan.producer_control_waiters);
		mutex_lock(&charlatan.producer_control_lock);
		mutex_lock(&charlatan.lock);
		if (!charlatan.period_ms) {
			mutex_unlock(&charlatan.lock);
			mutex_unlock(&charlatan.producer_control_lock);
			atomic_dec(&charlatan.producer_control_waiters);
			return -EINVAL;
		}
		charlatan.producer_running = true;
		resume_period = charlatan.period_ms;
		mutex_unlock(&charlatan.lock);
		mod_delayed_work(system_wq, &charlatan.producer_work,
				 msecs_to_jiffies(resume_period));
		mutex_unlock(&charlatan.producer_control_lock);
		atomic_dec(&charlatan.producer_control_waiters);
		return 0;
	case CHARLATAN_IOC_INJECT_OVERFLOW:
		if (copy_from_user(&overflow, (void __user *)argument, sizeof(overflow)))
			return -EFAULT;
		if (overflow > CHARLATAN_MAX_OVERFLOW_INJECT)
			return -EINVAL;
		mutex_lock(&charlatan.lock);
		for (index = 0; index < overflow; index++)
			wake |= charlatan_enqueue_locked(index, 0, true);
		mutex_unlock(&charlatan.lock);
		if (wake)
			wake_up_interruptible(&charlatan.read_wait);
		return 0;
	case CHARLATAN_IOC_SET_FAULTS:
		if (copy_from_user(&faults, (void __user *)argument, sizeof(faults)))
			return -EFAULT;
		if (faults.abi_version != CHARLATAN_ABI_VERSION ||
		    faults.producer_control_delay_ms > CHARLATAN_MAX_CONTROL_DELAY_MS)
			return -EINVAL;
		mutex_lock(&charlatan.lock);
		charlatan.fail_next_reads = faults.fail_next_reads;
		charlatan.fail_next_writes = faults.fail_next_writes;
		charlatan.producer_control_delay_ms = faults.producer_control_delay_ms;
		mutex_unlock(&charlatan.lock);
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations charlatan_fops = {
	.owner = THIS_MODULE,
	.open = charlatan_open,
	.release = charlatan_release,
	.read = charlatan_read,
	.write = charlatan_write,
	.poll = charlatan_poll,
	.unlocked_ioctl = charlatan_ioctl,
	.llseek = noop_llseek,
};

static int __init charlatan_init(void)
{
	struct device *device;
	int error;

	mutex_init(&charlatan.lock);
	mutex_init(&charlatan.producer_control_lock);
	atomic_set(&charlatan.producer_control_waiters, 0);
	init_waitqueue_head(&charlatan.read_wait);
	INIT_DELAYED_WORK(&charlatan.producer_work, charlatan_producer);
	error = alloc_chrdev_region(&charlatan_devt, 0, 1, CHARLATAN_DEVICE_NAME);
	if (error)
		return error;
	cdev_init(&charlatan_cdev, &charlatan_fops);
	charlatan_cdev.owner = THIS_MODULE;
	error = cdev_add(&charlatan_cdev, charlatan_devt, 1);
	if (error)
		goto unregister_region;
	charlatan_class = class_create(CHARLATAN_DEVICE_NAME);
	if (IS_ERR(charlatan_class)) {
		error = PTR_ERR(charlatan_class);
		goto delete_cdev;
	}
	device = device_create(charlatan_class, NULL, charlatan_devt, NULL, CHARLATAN_DEVICE_NAME);
	if (IS_ERR(device)) {
		error = PTR_ERR(device);
		goto destroy_class;
	}
	pr_info("charlatan: registered /dev/%s capacity=%u\n", CHARLATAN_DEVICE_NAME,
		CHARLATAN_QUEUE_CAPACITY);
	return 0;
destroy_class:
	class_destroy(charlatan_class);
delete_cdev:
	cdev_del(&charlatan_cdev);
unregister_region:
	unregister_chrdev_region(charlatan_devt, 1);
	return error;
}

static void __exit charlatan_exit(void)
{
	mutex_lock(&charlatan.producer_control_lock);
	mutex_lock(&charlatan.lock);
	charlatan.shutting_down = true;
	mutex_unlock(&charlatan.lock);
	wake_up_interruptible(&charlatan.read_wait);
	cancel_delayed_work_sync(&charlatan.producer_work);
	mutex_unlock(&charlatan.producer_control_lock);
	device_destroy(charlatan_class, charlatan_devt);
	class_destroy(charlatan_class);
	cdev_del(&charlatan_cdev);
	unregister_chrdev_region(charlatan_devt, 1);
	pr_info("charlatan: unregistered\n");
}

module_init(charlatan_init);
module_exit(charlatan_exit);

MODULE_DESCRIPTION("Charlatan virtual streaming character device");
MODULE_AUTHOR("Charlatan contributors");
MODULE_LICENSE("GPL");
