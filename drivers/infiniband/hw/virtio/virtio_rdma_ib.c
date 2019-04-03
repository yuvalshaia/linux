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

#include <linux/scatterlist.h>
#include <linux/virtio.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>

#include "virtio_rdma.h"
#include "virtio_rdma_device.h"

/* TODO: Move to uapi header file */

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */

#define VIRTIO_RDMA_CTRL_OK	0
#define VIRTIO_RDMA_CTRL_ERR	1

struct control_buf {
	__u8 cmd;
	__u8 status;
};

enum {
	VIRTIO_CMD_QUERY_DEVICE = 10,
	VIRTIO_CMD_QUERY_PORT,
};

struct cmd_query_port {
	__u8 port;
};
/* TODO: Move to uapi header file */

/* TODO: For the scope fof the RFC i'm utilizing ib*_*_attr structures */

static int virtio_rdma_exec_cmd(struct virtio_rdma_info *di, int cmd,
				struct scatterlist *in, struct scatterlist *out)
{
	struct scatterlist *sgs[4], hdr, status;
	struct control_buf *ctrl;
	unsigned tmp;
	int rc;

	ctrl = kmalloc(sizeof(*ctrl), GFP_ATOMIC);
	ctrl->cmd = cmd;
	ctrl->status = ~0;

	sg_init_one(&hdr, &ctrl->cmd, sizeof(ctrl->cmd));
	sgs[0] = &hdr;
	sgs[1] = in;
	sgs[2] = out;
	sg_init_one(&status, &ctrl->status, sizeof(ctrl->status));
	sgs[3] = &status;

	rc = virtqueue_add_sgs(di->ctrl_vq, sgs, 2, 2, di, GFP_ATOMIC);
	if (rc)
		goto out;

	if (unlikely(!virtqueue_kick(di->ctrl_vq))) {
		goto out_with_status;
	}

	/* Spin for a response, the kick causes an ioport write, trapping
	 * into the hypervisor, so the request should be handled
	 * immediately */
	while (!virtqueue_get_buf(di->ctrl_vq, &tmp) &&
	       !virtqueue_is_broken(di->ctrl_vq))
		cpu_relax();

out_with_status:
	printk("%s: cmd %d, status %d\n", __func__, ctrl->cmd, ctrl->status);
	rc = ctrl->status == VIRTIO_RDMA_CTRL_OK ? 0 : 1;

out:
	kfree(ctrl);

	return rc;
}

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

static int virtio_rdma_query_device(struct ib_device *ibdev,
				    struct ib_device_attr *props,
				    struct ib_udata *uhw)
{
	struct scatterlist data;
	int offs;
	int rc;

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	/* We start with sys_image_guid because of inconsistency beween ib_
	 * and ibv_ */
	offs = offsetof(struct ib_device_attr, sys_image_guid);
	sg_init_one(&data, (void *)props + offs, sizeof(*props) - offs);

	rc = virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_QUERY_DEVICE, NULL,
				  &data);

	printk("%s: sys_image_guid 0x%llx\n", __func__,
	       be64_to_cpu(props->sys_image_guid));

	return rc;
}

static int virtio_rdma_query_port(struct ib_device *ibdev, u8 port,
				  struct ib_port_attr *props)
{
	struct scatterlist in, out;
	struct cmd_query_port *cmd;
	int offs;
	int rc;

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);

	/* We start with state because of inconsistency beween ib and ibv */
	offs = offsetof(struct ib_port_attr, state);
	sg_init_one(&out, (void *)props + offs, sizeof(*props) - offs);

	cmd->port = port;
	sg_init_one(&in, cmd, sizeof(cmd));
	printk("%s: port %d\n", __func__, cmd->port);

	rc = virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_QUERY_PORT, &in,
				  &out);

	printk("%s: gid_tbl_len %d\n", __func__, props->gid_tbl_len);

	kfree(cmd);

	return rc;
}

int virtio_rdma_query_gid(struct ib_device *ibdev, u8 port, int index,
			  union ib_gid *gid)
{
	memset(gid, 0, sizeof(union ib_gid));

	printk("%s: port %d, index %d\n", __func__, port, index);

	return 0;
}

static int virtio_rdma_add_gid(const struct ib_gid_attr *attr, void **context)
{
	printk("%s:\n", __func__);

	return 0;
}

struct ib_mr *virtio_rdma_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
				   u32 max_num_sg)
{
	printk("%s: mr_type %d, max_num_sg %d\n", __func__, mr_type,
	       max_num_sg);

	return NULL;
}

int virtio_rdma_alloc_pd(struct ib_pd *ibpd, struct ib_ucontext *context,
			 struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return 0;
}

struct ib_ah *virtio_rdma_create_ah(struct ib_pd *pd,
				    struct rdma_ah_attr *ah_attr, u32 flags,
				    struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return NULL;
}

struct ib_cq *virtio_rdma_create_cq(struct ib_device *ibdev,
				    const struct ib_cq_init_attr *attr,
				    struct ib_ucontext *context,
				    struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return NULL;
}

struct ib_qp *virtio_rdma_create_qp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return NULL;
}

void virtio_rdma_dealloc_pd(struct ib_pd *pd)
{
	printk("%s:\n", __func__);
}

void virtio_rdma_dealloc_ucontext(struct ib_ucontext *ibcontext)

{
}

static int virtio_rdma_del_gid(const struct ib_gid_attr *attr, void **context)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_dereg_mr(struct ib_mr *ibmr)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_destroy_ah(struct ib_ah *ah, u32 flags)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_destroy_cq(struct ib_cq *cq)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_destroy_qp(struct ib_qp *qp)
{
	printk("%s:\n", __func__);

	return 0;
}

static void virtio_rdma_get_fw_ver_str(struct ib_device *device, char *str)
{
	printk("%s:\n", __func__);
}

struct ib_mr *virtio_rdma_get_dma_mr(struct ib_pd *pd, int acc)

{
	printk("%s:\n", __func__);

	return 0;
}

enum rdma_link_layer virtio_rdma_port_link_layer(struct ib_device *ibdev,
						 u8 port)
{
	return IB_LINK_LAYER_ETHERNET;
}

static struct net_device *virtio_rdma_get_netdev(struct ib_device *ibdev,
						 u8 port_num)
{
	printk("%s:\n", __func__);

	return NULL;
}

int virtio_rdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
			  int sg_nents, unsigned int *sg_offset)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_modify_port(struct ib_device *ibdev, u8 port, int mask,
			    struct ib_port_modify *props)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			  int attr_mask, struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			   u16 *pkey)
{
	printk("%s:\n", __func__);

	return 0;
}

int virtio_rdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_qp_init_attr *init_attr)
{
	printk("%s:\n", __func__);

	return 0;
}

struct ib_mr *virtio_rdma_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				      u64 virt_addr, int access_flags,
				      struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return NULL;
}

int virtio_rdma_req_notify_cq(struct ib_cq *ibcq,
			      enum ib_cq_notify_flags notify_flags)
{
	printk("%s:\n", __func__);

	return 0;
}

static const struct ib_device_ops virtio_rdma_dev_ops = {
	.get_port_immutable = virtio_rdma_port_immutable,
	.query_device = virtio_rdma_query_device,
	.query_port = virtio_rdma_query_port,
	.query_gid = virtio_rdma_query_gid,
	.add_gid = virtio_rdma_add_gid,
	.alloc_mr = virtio_rdma_alloc_mr,
	.alloc_pd = virtio_rdma_alloc_pd,
	.alloc_ucontext = virtio_rdma_alloc_ucontext,
	.create_ah = virtio_rdma_create_ah,
	.create_cq = virtio_rdma_create_cq,
	.create_qp = virtio_rdma_create_qp,
	.dealloc_pd = virtio_rdma_dealloc_pd,
	.dealloc_ucontext = virtio_rdma_dealloc_ucontext,
	.del_gid = virtio_rdma_del_gid,
	.dereg_mr = virtio_rdma_dereg_mr,
	.destroy_ah = virtio_rdma_destroy_ah,
	.destroy_cq = virtio_rdma_destroy_cq,
	.destroy_qp = virtio_rdma_destroy_qp,
	.get_dev_fw_str = virtio_rdma_get_fw_ver_str,
	.get_dma_mr = virtio_rdma_get_dma_mr,
	.get_link_layer = virtio_rdma_port_link_layer,
	.get_netdev = virtio_rdma_get_netdev,
	.get_port_immutable = virtio_rdma_port_immutable,
	.map_mr_sg = virtio_rdma_map_mr_sg,
	.mmap = virtio_rdma_mmap,
	.modify_port = virtio_rdma_modify_port,
	.modify_qp = virtio_rdma_modify_qp,
	.poll_cq = virtio_rdma_poll_cq,
	.post_recv = virtio_rdma_post_recv,
	.post_send = virtio_rdma_post_send,
	.query_device = virtio_rdma_query_device,
	.query_pkey = virtio_rdma_query_pkey,
	.query_port = virtio_rdma_query_port,
	.query_qp = virtio_rdma_query_qp,
	.reg_user_mr = virtio_rdma_reg_user_mr,
	.req_notify_cq = virtio_rdma_req_notify_cq,
};

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s-%s\n", VIRTIO_RDMA_HW_NAME,
		       VIRTIO_RDMA_DRIVER_VER);
}
static DEVICE_ATTR_RO(hca_type);

static ssize_t hw_rev_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", VIRTIO_RDMA_HW_REV);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t board_id_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", VIRTIO_RDMA_BOARD_ID);
}
static DEVICE_ATTR_RO(board_id);

static struct attribute *virtio_rdmaa_class_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	&dev_attr_board_id.attr,
	NULL,
};

static const struct attribute_group virtio_rdmaa_attr_group = {
	.attrs = virtio_rdmaa_class_attributes,
};

int init_ib(struct virtio_rdma_info *dev)
{
	int rc;

	dev->ib_dev.owner = THIS_MODULE;
	dev->ib_dev.num_comp_vectors = 1;
	dev->ib_dev.dev.parent = &dev->vdev->dev;
	dev->ib_dev.node_type = RDMA_NODE_IB_CA;
	dev->ib_dev.phys_port_cnt = 1;
	dev->ib_dev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT);

	rdma_set_device_sysfs_group(&dev->ib_dev, &virtio_rdmaa_attr_group);

	ib_set_device_ops(&dev->ib_dev, &virtio_rdma_dev_ops);

	rc = ib_register_device(&dev->ib_dev, "virtio_rdma%d");

	return rc;
}

void fini_ib(struct virtio_rdma_info *dev)
{
	ib_unregister_device(&dev->ib_dev);
}
