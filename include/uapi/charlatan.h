/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef CHARLATAN_UAPI_H
#define CHARLATAN_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CHARLATAN_ABI_VERSION 5U
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
	__u32 reserved;
	__u64 produced;
	__u64 consumed;
	__u64 dropped;
	__u64 reset_discards;
	__u64 resets;
};

#define CHARLATAN_IOC_MAGIC 'C'
#define CHARLATAN_IOC_RESET _IO(CHARLATAN_IOC_MAGIC, 0x01)
#define CHARLATAN_IOC_GET_STATS _IOR(CHARLATAN_IOC_MAGIC, 0x02, struct charlatan_stats)
/*
 * Commands 0x03 through 0x07 (SET_RATE, PAUSE, RESUME, INJECT_OVERFLOW,
 * SET_FAULTS) belonged to ABI version 4.  They controlled the module's
 * built-in event producer and fault countdowns, both of which were removed in
 * ABI version 5: events now enter the queue only through write(), and the
 * recovery paths they exercised no longer exist.
 */

#endif
