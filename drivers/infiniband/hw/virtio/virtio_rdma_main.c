/*
 * RDMA driver for virtio-rdma device
 *  Copyright (C) 2019 Yuval Shaia Oracle Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/module.h>
#include <uapi/linux/virtio_ids.h>

static int virtrdma_probe(struct virtio_device *vdev)
{
	pr_info("VirtIO RDMA device %d probed\n", vdev->index);
	return 0;
}

static void virtrdma_remove(struct virtio_device *vdev)
{
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
	if (!rc) {
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
MODULE_DESCRIPTION("Virtio RDMA number driver");
MODULE_LICENSE("GPL");
