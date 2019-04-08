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

#ifndef __VIRTIO_RDMA_IB__
#define __VIRTIO_RDMA_IB__

#include <rdma/ib_verbs.h>

struct virtio_rdma_pd {
	struct ib_pd ibpd;
	u32 pd_handle;
};

struct virtio_rdma_user_mr {
	struct ib_mr ibmr;
	u32 mr_handle;
};

static inline struct virtio_rdma_pd *to_vpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct virtio_rdma_pd, ibpd);
}

int init_ib(struct virtio_rdma_info *ri);
void fini_ib(struct virtio_rdma_info *ri);

#endif
