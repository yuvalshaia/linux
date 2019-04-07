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

#include "virtio_rdma_netdev.h"

int init_netdev(struct virtio_rdma_info *ri)
{
	struct net_device *dev;
	struct virtio_rdma_netdev_info *vrndi;

	dev = alloc_etherdev(sizeof(struct virtio_rdma_netdev_info));
	if (!dev) {
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, &ri->vdev->dev);
	vrndi = netdev_priv(dev);
	vrndi->ri = ri;
	ri->netdev = dev;

	return 0;
}

void fini_netdev(struct virtio_rdma_info *ri)
{
	unregister_netdev(ri->netdev);
}
