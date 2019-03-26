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
	struct virtio_rdma_info *ri;
	int rc = -EIO;

	ri = ib_alloc_device(virtio_rdma_info, ib_dev);
	if (!ri) {
		pr_err("Fail to allocate IB device\n");
		rc = -ENOMEM;
		goto out;
	}
	vdev->priv = ri;

	ri->vdev = vdev;

	rc = init_device(ri);
	if (rc) {
		pr_err("Fail to connect to device\n");
		goto out_dealloc_ib_device;
	}

	rc = init_ib(ri);
	if (rc) {
		pr_err("Fail to connect to IB layer\n");
		goto out_fini_device;
	}

	pr_info("VirtIO RDMA device %d probed\n", vdev->index);

	goto out;

out_fini_device:
	fini_device(ri);

out_dealloc_ib_device:
	ib_dealloc_device(&ri->ib_dev);

	vdev->priv = NULL;

out:
	return rc;
}

static void virtrdma_remove(struct virtio_device *vdev)
{
	struct virtio_rdma_info *ri = vdev->priv;

	if (!ri)
		return;

	vdev->priv = NULL;

	fini_ib(ri);

	fini_device(ri);

	ib_dealloc_device(&ri->ib_dev);

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
