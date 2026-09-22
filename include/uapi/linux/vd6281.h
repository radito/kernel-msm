/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _UAPI_LINUX_VD6281_H
#define _UAPI_LINUX_VD6281_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MAX_RAINBOW_CONFIG_SIZE 32

enum rainbow_op_type {
	RAINBOW_SEQ_READ,
	RAINBOW_RANDOM_READ,
	RAINBOW_SEQ_WRITE,
	RAINBOW_RANDOM_WRITE,
	RAINBOW_ENABLE
};

struct rainbow_config {
	enum rainbow_op_type operation;
	__u32 size;
	__u32 reg_addr[MAX_RAINBOW_CONFIG_SIZE];
	__u32 reg_data[MAX_RAINBOW_CONFIG_SIZE];
} __attribute__((packed));

#define RAINBOW_CONFIG \
	_IOWR('R', 1, struct rainbow_config)

#endif
