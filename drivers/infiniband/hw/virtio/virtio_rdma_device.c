/*
 * Virtio RDMA device: Device related functions and data
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

#include <linux/virtio_config.h>

#include "virtio_rdma.h"

static void rdma_ctrl_ack(struct virtqueue *vq)
{
	struct virtio_rdma_info *dev = vq->vdev->priv;

	wake_up(&dev->acked);

	printk("%s\n", __func__);
}

int init_device(struct virtio_rdma_info *dev)
{
#define TMP_MAX_VQ 1
	int rc;
	struct virtqueue *vqs[TMP_MAX_VQ];
	vq_callback_t *callbacks[TMP_MAX_VQ];
	const char *names[TMP_MAX_VQ];

	names[0] = "ctrl";
	callbacks[0] = rdma_ctrl_ack;
	callbacks[0] = NULL;

	rc = dev->vdev->config->find_vqs(dev->vdev, TMP_MAX_VQ, vqs, callbacks,
					 names, NULL, NULL);
	if (rc)
		return rc;

	dev->ctrl_vq = vqs[0];

	return 0;
}

void fini_device(struct virtio_rdma_info *dev)
{
	dev->vdev->config->reset(dev->vdev);
	dev->vdev->config->del_vqs(dev->vdev);
}
