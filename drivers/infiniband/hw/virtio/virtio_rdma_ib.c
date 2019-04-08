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
#include <rdma/ib_mad.h>

#include "virtio_rdma.h"
#include "virtio_rdma_device.h"
#include "virtio_rdma_ib.h"

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
	VIRTIO_CMD_CREATE_CQ,
	VIRTIO_CMD_DESTROY_CQ,
	VIRTIO_CMD_CREATE_PD,
	VIRTIO_CMD_DESTROY_PD,
	VIRTIO_CMD_GET_DMA_MR,
};

struct cmd_query_port {
	__u8 port;
};

struct cmd_create_cq {
	__u32 cqe;
};

struct rsp_create_cq {
	__u32 cqn;
};

struct cmd_destroy_cq {
	__u32 cqn;
};

struct rsp_create_pd {
	__u32 pdn;
};

struct cmd_destroy_pd {
	__u32 pdn;
};

struct cmd_get_dma_mr {
	__u32 pdn;
	__u32 access_flags;
};

struct rsp_get_dma_mr {
	__u32 mrn;
	__u32 lkey;
	__u32 rkey;
};

/* TODO: Move to uapi header file */

struct virtio_rdma_ib_cq {
	struct ib_cq ibcq;
	u32 cq_handle;
};

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
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

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
	if (!cmd)
		return -ENOMEM;

	/* We start with state because of inconsistency beween ib and ibv */
	offs = offsetof(struct ib_port_attr, state);
	sg_init_one(&out, (void *)props + offs, sizeof(*props) - offs);

	cmd->port = port;
	sg_init_one(&in, cmd, sizeof(*cmd));
	printk("%s: port %d\n", __func__, cmd->port);

	rc = virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_QUERY_PORT, &in,
				  &out);

	printk("%s: gid_tbl_len %d\n", __func__, props->gid_tbl_len);

	kfree(cmd);

	return rc;
}

static struct net_device *virtio_rdma_get_netdev(struct ib_device *ibdev,
						 u8 port_num)
{
	struct virtio_rdma_info *ri = to_vdev(ibdev);

	printk("%s:\n", __func__);

	return ri->netdev;
}

struct ib_cq *virtio_rdma_create_cq(struct ib_device *ibdev,
				    const struct ib_cq_init_attr *attr,
				    struct ib_ucontext *context,
				    struct ib_udata *udata)
{
	struct scatterlist in, out;
	struct virtio_rdma_ib_cq *vcq;
	struct cmd_create_cq *cmd;
	struct rsp_create_cq *rsp;
	struct ib_cq *cq = NULL;
	int rc;

	/* TODO: Check MAX_CQ */

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return ERR_PTR(-ENOMEM);

	rsp = kmalloc(sizeof(*rsp), GFP_ATOMIC);
	if (!rsp) {
		kfree(cmd);
		return ERR_PTR(-ENOMEM);
	}

	vcq = kzalloc(sizeof(*vcq), GFP_KERNEL);
	if (!vcq)
		goto out;

	cmd->cqe = attr->cqe;
	sg_init_one(&in, cmd, sizeof(*cmd));
	printk("%s: cqe %d\n", __func__, cmd->cqe);

	sg_init_one(&out, rsp, sizeof(*rsp));

	rc = virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_CREATE_CQ, &in,
				  &out);
	if (rc)
		goto out_err;

	printk("%s: cqn 0x%x\n", __func__, rsp->cqn);
	vcq->cq_handle = rsp->cqn;
	vcq->ibcq.cqe = attr->cqe;
	cq = &vcq->ibcq;

	goto out;

out_err:
	kfree(vcq);
	return ERR_PTR(rc);

out:
	kfree(rsp);
	kfree(cmd);
	return cq;
}

int virtio_rdma_destroy_cq(struct ib_cq *cq)
{
	struct virtio_rdma_ib_cq *vcq;
	struct scatterlist in;
	struct cmd_destroy_cq *cmd;
	int rc;

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	vcq = container_of(cq, struct virtio_rdma_ib_cq, ibcq);

	cmd->cqn = vcq->cq_handle;
	sg_init_one(&in, cmd, sizeof(*cmd));

	rc = virtio_rdma_exec_cmd(to_vdev(cq->device), VIRTIO_CMD_DESTROY_CQ,
				  &in, NULL);

	kfree(cmd);

	kfree(vcq);

	return rc;
}

int virtio_rdma_alloc_pd(struct ib_pd *ibpd, struct ib_ucontext *context,
			 struct ib_udata *udata)
{
	struct virtio_rdma_pd *pd = to_vpd(ibpd);
	struct ib_device *ibdev = ibpd->device;
	struct rsp_create_pd *rsp;
	struct scatterlist out;
	int rc;

	/* TODO: Check MAX_PD */

	rsp = kmalloc(sizeof(*rsp), GFP_ATOMIC);
	if (!rsp)
		return -ENOMEM;

	sg_init_one(&out, rsp, sizeof(*rsp));

	rc = virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_CREATE_PD, NULL,
				  &out);
	if (rc)
		goto out;

	pd->pd_handle = rsp->pdn;

	printk("%s: pd_handle=%d\n", __func__, pd->pd_handle);

out:
	kfree(rsp);

	printk("%s: rc=%d\n", __func__, rc);
	return rc;
}

void virtio_rdma_dealloc_pd(struct ib_pd *pd)
{
	struct virtio_rdma_pd *vpd = to_vpd(pd);
	struct ib_device *ibdev = pd->device;
	struct cmd_destroy_pd *cmd;
	struct scatterlist in;

	printk("%s:\n", __func__);

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return;

	cmd->pdn = vpd->pd_handle;
	sg_init_one(&in, cmd, sizeof(*cmd));

	virtio_rdma_exec_cmd(to_vdev(ibdev), VIRTIO_CMD_DESTROY_PD, &in, NULL);

	kfree(cmd);
}

struct ib_mr *virtio_rdma_get_dma_mr(struct ib_pd *pd, int acc)

{
	struct virtio_rdma_user_mr *mr;
	struct scatterlist in, out;
	struct cmd_get_dma_mr *cmd = NULL;
	struct rsp_get_dma_mr *rsp = NULL;
	int rc;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd) {
		kfree(mr);
		return ERR_PTR(-ENOMEM);
	}

	rsp = kmalloc(sizeof(*rsp), GFP_ATOMIC);
	if (!cmd) {
		kfree(mr);
		kfree(cmd);
		return ERR_PTR(-ENOMEM);
	}

	cmd->pdn = to_vpd(pd)->pd_handle;
	cmd->access_flags = acc;
	sg_init_one(&in, cmd, sizeof(*cmd));

	sg_init_one(&out, rsp, sizeof(*rsp));

	rc = virtio_rdma_exec_cmd(to_vdev(pd->device), VIRTIO_CMD_GET_DMA_MR,
				  &in, &out);
	if (rc) {
		kfree(mr);
		kfree(cmd);
		return ERR_PTR(rc);
	}

	mr->mr_handle = rsp->mrn;
	mr->ibmr.lkey = rsp->lkey;
	mr->ibmr.rkey = rsp->rkey;

	printk("%s: mr_handle=0x%x\n", __func__, mr->mr_handle);

	kfree(cmd);
	kfree(rsp);

	return &mr->ibmr;
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

struct ib_qp *virtio_rdma_create_qp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	printk("%s:\n", __func__);

	return NULL;
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

struct virtio_rdma_cq {
	struct ib_cq ibcq;
};

int virtio_rdma_destroy_qp(struct ib_qp *qp)
{
	printk("%s:\n", __func__);

	return 0;
}

static void virtio_rdma_get_fw_ver_str(struct ib_device *device, char *str)
{
	printk("%s:\n", __func__);
}

enum rdma_link_layer virtio_rdma_port_link_layer(struct ib_device *ibdev,
						 u8 port)
{
	return IB_LINK_LAYER_ETHERNET;
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
	.get_netdev = virtio_rdma_get_netdev,
	.create_cq = virtio_rdma_create_cq,
	.destroy_cq = virtio_rdma_destroy_cq,
	.alloc_pd = virtio_rdma_alloc_pd,
	.dealloc_pd = virtio_rdma_dealloc_pd,
	.get_dma_mr = virtio_rdma_get_dma_mr,
	.query_gid = virtio_rdma_query_gid,
	.add_gid = virtio_rdma_add_gid,
	.alloc_mr = virtio_rdma_alloc_mr,
	.alloc_ucontext = virtio_rdma_alloc_ucontext,
	.create_ah = virtio_rdma_create_ah,
	.create_qp = virtio_rdma_create_qp,
	.dealloc_ucontext = virtio_rdma_dealloc_ucontext,
	.del_gid = virtio_rdma_del_gid,
	.dereg_mr = virtio_rdma_dereg_mr,
	.destroy_ah = virtio_rdma_destroy_ah,
	.destroy_qp = virtio_rdma_destroy_qp,
	.get_dev_fw_str = virtio_rdma_get_fw_ver_str,
	.get_link_layer = virtio_rdma_port_link_layer,
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
	INIT_RDMA_OBJ_SIZE(ib_pd, virtio_rdma_pd, ibpd),
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

int init_ib(struct virtio_rdma_info *ri)
{
	int rc;

	ri->ib_dev.owner = THIS_MODULE;
	ri->ib_dev.num_comp_vectors = 1;
	ri->ib_dev.dev.parent = &ri->vdev->dev;
	ri->ib_dev.node_type = RDMA_NODE_IB_CA;
	ri->ib_dev.phys_port_cnt = 1;
	ri->ib_dev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD);

	rdma_set_device_sysfs_group(&ri->ib_dev, &virtio_rdmaa_attr_group);

	ib_set_device_ops(&ri->ib_dev, &virtio_rdma_dev_ops);

	rc = ib_register_device(&ri->ib_dev, "virtio_rdma%d");

	return rc;
}

void fini_ib(struct virtio_rdma_info *ri)
{
	ib_unregister_device(&ri->ib_dev);
}
