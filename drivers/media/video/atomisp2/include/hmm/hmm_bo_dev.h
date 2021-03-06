/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef	__HMM_BO_DEV_H__
#define	__HMM_BO_DEV_H__

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include "mmu/isp_mmu.h"
#include "hmm/hmm_common.h"
#include "hmm/hmm_vm.h"

#define	check_bodev_null_return(bdev, exp)	\
		check_null_return(bdev, exp, \
			"NULL hmm_bo_device.\n")

#define	check_bodev_null_return_void(bdev)	\
		check_null_return_void(bdev, \
			"NULL hmm_bo_device.\n")

#define	HMM_BO_DEVICE_INITED	0x1

struct hmm_buffer_object;

struct hmm_bo_device {
	/* isp_mmu provides lock itself */
	struct isp_mmu		mmu;

	/* hmm_vm provides lock itself */
	struct hmm_vm		vaddr_space;

	struct list_head	free_bo_list;
	struct list_head	active_bo_list;

	/* list lock is used to protect both of the buffer object lists */
	struct spinlock		list_lock;
#ifdef CONFIG_ION
	struct ion_client	*iclient;
#endif
	int			flag;
	struct mutex  mm_lock;
};

int hmm_bo_device_init(struct hmm_bo_device *bdev,
		       struct isp_mmu_client *mmu_driver,
		       unsigned int vaddr_start, unsigned int size);

/*
 * clean up all hmm_bo_device related things.
 */
void hmm_bo_device_exit(struct hmm_bo_device *bdev);

/*
 * whether the bo device is inited or not.
 */
int hmm_bo_device_inited(struct hmm_bo_device *bdev);

/*
 * find the buffer object with virtual address vaddr.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_start(
		struct hmm_bo_device *bdev, unsigned int vaddr);

/*
 * find the buffer object with virtual address vaddr.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_in_range(
		struct hmm_bo_device *bdev, unsigned int vaddr);

/*
 * find a buffer object with pgnr pages from free_bo_list and
 * activate it (remove from free_bo_list and add to
 * active_bo_list)
 *
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_get_bo(
		struct hmm_bo_device *bdev, unsigned int pgnr);

/*
 * destroy all buffer objects in the free_bo_list.
 */
void hmm_bo_device_destroy_free_bo_list(struct hmm_bo_device *bdev);
/*
 * destroy buffer object with start virtual address vaddr.
 */
void hmm_bo_device_destroy_free_bo_addr(struct hmm_bo_device *bdev,
		unsigned int vaddr);
/*
 * destroy all buffer objects with pgnr pages.
 */
void hmm_bo_device_destroy_free_bo_size(struct hmm_bo_device *bdev,
		unsigned int pgnr);

#endif
