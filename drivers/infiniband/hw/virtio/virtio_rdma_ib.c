/*
 * Virtio RDMA device: IB related functions and data
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

#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>

#include "virtio_rdma.h"

static int virtio_rdma_port_immutable(struct ib_device *ibdev, u8 port_num,
				      struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int rc;

	rc = ib_query_port(ibdev, port_num, &attr);
	if (rc)
		return rc;

	immutable->core_cap_flags |= RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	/* immutable->max_mad_size = IB_MGMT_MAD_SIZE; */

	return 0;
}

int virtio_rdma_query_device(struct ib_device *ibdev,
			     struct ib_device_attr *props, struct ib_udata *uhw)
{
	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	return 0;
}

int virtio_rdma_query_port(struct ib_device *ibdev, u8 port,
			   struct ib_port_attr *props)
{
	return 0;
}

int virtio_rdma_query_gid(struct ib_device *ibdev, u8 port, int index,
			  union ib_gid *gid)
{
	memset(gid, 0, sizeof(union ib_gid));

	return 0;
}

static const struct ib_device_ops virtio_rdma_dev_ops = {
	.get_port_immutable = virtio_rdma_port_immutable,
	.query_device = virtio_rdma_query_device,
	.query_port = virtio_rdma_query_port,
	.query_gid = virtio_rdma_query_gid,
};

int init_ib(struct virtio_rdma_info *dev)
{
	int rc;

	dev->ib_dev.owner = THIS_MODULE;
	dev->ib_dev.num_comp_vectors = 1;
	dev->ib_dev.phys_port_cnt = 1;
	dev->ib_dev.dev.parent = &dev->vdev->dev;
	dev->ib_dev.node_type = RDMA_NODE_IB_CA;

	ib_set_device_ops(&dev->ib_dev, &virtio_rdma_dev_ops);

	rc = ib_register_device(&dev->ib_dev, "virtio_rdma%d");

	return 0;
}

void fini_ib(struct virtio_rdma_info *dev)
{
	ib_unregister_device(&dev->ib_dev);
}
