/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef CHARLATAN_UAPI_H
#define CHARLATAN_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CHARLATAN_ABI_VERSION 4U
#define CHARLATAN_MMAP_ABI_VERSION 1U
#define CHARLATAN_EVENT_BYTES 24U
#define CHARLATAN_MMAP_SNAPSHOT_EVENTS 128U

struct charlatan_event {
	__u64 sequence;
	__u64 produced_ns;
	__u32 value;
	__u32 flags;
};

/*
 * Read-only observation page exposed by mmap(offset=0).
 *
 * The kernel publishes an odd version while it is copying a snapshot and an
 * even version after the copy is complete.  Readers must load version with
 * acquire semantics, copy the fields, load version again, and accept the
 * snapshot only when both values are equal and even.
 */
struct charlatan_mmap_snapshot {
	__u64 version;
	__u64 reset_generation;
	__u64 next_sequence;
	__u32 abi_version;
	__u32 event_size;
	__u32 queue_capacity;
	__u32 queue_depth;
	struct charlatan_event events[CHARLATAN_MMAP_SNAPSHOT_EVENTS];
};

struct charlatan_stats {
	__u32 abi_version;
	__u32 queue_capacity;
	__u32 queue_depth;
	__u32 producer_period_ms;
	__u64 produced;
	__u64 injected;
	__u64 consumed;
	__u64 dropped;
	__u64 reset_discards;
	__u64 resets;
	__u64 read_failures;
	__u64 write_failures;
	__u32 producer_running;
	__u32 producer_control_waiters;
	__u32 producer_control_delay_active;
	__u32 reserved;
};

struct charlatan_rate_config {
	__u32 abi_version;
	__u32 period_ms;
};

struct charlatan_fault_config {
	__u32 abi_version;
	__u32 fail_next_reads;
	__u32 fail_next_writes;
	__u32 producer_control_delay_ms;
};

#define CHARLATAN_IOC_MAGIC 'C'
#define CHARLATAN_IOC_RESET _IO(CHARLATAN_IOC_MAGIC, 0x01)
#define CHARLATAN_IOC_GET_STATS _IOR(CHARLATAN_IOC_MAGIC, 0x02, struct charlatan_stats)
#define CHARLATAN_IOC_SET_RATE _IOW(CHARLATAN_IOC_MAGIC, 0x03, struct charlatan_rate_config)
#define CHARLATAN_IOC_PAUSE _IO(CHARLATAN_IOC_MAGIC, 0x04)
#define CHARLATAN_IOC_RESUME _IO(CHARLATAN_IOC_MAGIC, 0x05)
#define CHARLATAN_IOC_INJECT_OVERFLOW _IOW(CHARLATAN_IOC_MAGIC, 0x06, __u32)
#define CHARLATAN_IOC_SET_FAULTS _IOW(CHARLATAN_IOC_MAGIC, 0x07, struct charlatan_fault_config)

#endif
