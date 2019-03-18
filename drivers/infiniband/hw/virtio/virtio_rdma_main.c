/*
 * Virtio RDMA device
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

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/module.h>
#include <uapi/linux/virtio_ids.h>

#include "virtio_rdma.h"
#include "virtio_rdma_device.h"
#include "virtio_rdma_ib.h"

/* TODO:
 * - How to hook to unload driver, we need to undo all the stuff with did
 *   for all the devices that probed
 * -
 */

static int virtrdma_probe(struct virtio_device *vdev)
{
	struct virtio_rdma_info *dev;
	int rc;
	
	pr_info("VirtIO RDMA device %d probed\n", vdev->index);

	dev = ib_alloc_device(virtio_rdma_info, ib_dev);
	if (!dev) {
		pr_err("Fail to allocate IB device\n");
		rc = -ENOMEM;
		goto out_dealloc_ib_device;
	}
	vdev->priv = dev;

	dev->vdev = vdev;

	rc = init_device(dev);
	if (rc) {
		pr_err("Fail to connect to device\n");
		goto out_dealloc_ib_device;
	}

	rc = init_ib(dev);
	if (rc) {
		pr_err("Fail to connect to IB layer\n");
		goto out_fini_device;
	}

	return 0;

out_fini_device:
	fini_device(dev);

out_dealloc_ib_device:
	fini_ib(dev);

	vdev->priv = NULL;

	return rc;
}

static void virtrdma_remove(struct virtio_device *vdev)
{
	struct virtio_rdma_info *dev = vdev->priv;

	if (!dev)
		return;

	fini_ib(dev);

	fini_device(dev);

	vdev->priv = NULL;

	pr_info("VirtIO RDMA device %d removed\n", vdev->index);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_RDMA, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_rdma_driver = {
	.driver.name	= KBUILD_MODNAME,
	.driver.owner	= THIS_MODULE,
	.id_table	= id_table,
	.probe		= virtrdma_probe,
	.remove		= virtrdma_remove,
};

static int __init virtrdma_init(void)
{
	int rc;

	rc = register_virtio_driver(&virtio_rdma_driver);
	if (rc) {
		pr_err("%s: Fail to register virtio driver (%d)\n", __func__,
		       rc);
		return rc;
	}

	return 0;
}

static void __exit virtrdma_fini(void)
{
	unregister_virtio_driver(&virtio_rdma_driver);
}

module_init(virtrdma_init);
module_exit(virtrdma_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_AUTHOR("Yuval Shaia");
MODULE_DESCRIPTION("Virtio RDMA driver");
MODULE_LICENSE("Dual BSD/GPL");
