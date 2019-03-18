/*
 * Virtio RDMA device: Driver main data types
 *
 * Copyright (C) 2019 Yuval Shaia Oracle Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __VIRTIO_RDMA__
#define __VIRTIO_RDMA__

#include <linux/virtio.h>
#include <rdma/ib_verbs.h>

struct virtio_rdma_info {
	struct ib_device ib_dev;
	struct virtio_device *vdev;
};

static inline struct virtio_rdma_info *to_vdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct virtio_rdma_info, ib_dev);
}

#endif
