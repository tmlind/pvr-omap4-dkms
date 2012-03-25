/*
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>

#ifndef LINUX
#define LINUX	/* Needed by IMG headers */
#endif
#include "pvrmodule.h"
#include "img_defs.h"
#include "servicesext.h"
#include "kernelbuffer.h"
#include "gfx_bc.h"
#include "v4gfx.h"

#define BCLOGNM "v4l2-gfx bc: "

#define BCERR(fmt, arg...) printk(KERN_ERR BCLOGNM fmt, ## arg)

#define BCLOG(fmt, arg...)	\
do {	\
	if (debug >= 1)	\
		printk(KERN_INFO BCLOGNM fmt, ## arg);	\
} while (0)


struct bc_buffer {
	u32 size;
	unsigned long *paddrp;		/* physical addr. array */
	PVRSRV_SYNC_DATA *pvr_sync_data;
};

struct gfx_bc_devinfo {
	struct bc_buffer bc_buf[VIDEO_MAX_FRAME];
	int ref;
	int num_bufs;
	int ref_cnt;

	/* PVR data types  */
	IMG_UINT32 pvr_id;
	BUFFER_INFO pvr_bcinfo;
	PVRSRV_BC_SRV2BUFFER_KMJTABLE pvr_s2b_jt;
};

static struct gfx_bc_devinfo *g_devices[DEVICE_COUNT];
static PVRSRV_BC_BUFFER2SRV_KMJTABLE pvr_b2s_jt; /* Jump table from driver to SGX */
static int bc_initialized = -1;

/*
 * Service to Buffer Device API - this section covers the entry points from
 * the SGX kernel services to this driver
 */
static PVRSRV_ERROR s2b_open_bc_device(IMG_UINT32 ui32DeviceID,
				       IMG_HANDLE *hdevicep)
{
	struct gfx_bc_devinfo *devinfo;
	int idx;

	/* Search for device in g_devices and return it */
	for (idx = 0; idx < DEVICE_COUNT; idx++) {
		devinfo = g_devices[idx];
		if (devinfo->pvr_id ==  ui32DeviceID) {
			*hdevicep = (IMG_HANDLE)devinfo;
			return PVRSRV_OK;
		}
	}

	/* Didn't find the device id in g_devices */
	BCERR("Open of bc device %d failed, not found in g_devices\n",
	      ui32DeviceID);
	return -EINVAL;
}

static PVRSRV_ERROR s2b_close_bc_device(IMG_UINT32 ui32DeviceID,
					IMG_HANDLE hdevice)
{
	PVR_UNREFERENCED_PARAMETER(hdevice);
	return PVRSRV_OK;
}

static PVRSRV_ERROR s2b_get_bc_buffer(IMG_HANDLE hdevice,
				      IMG_UINT32 bufno,
				      PVRSRV_SYNC_DATA *pvr_sync_data,
				      IMG_HANDLE *hbufferp)
{
	struct gfx_bc_devinfo *devinfo;
	BCLOG("+%s\n", __func__);

	if (!hdevice || !hbufferp)
		return PVRSRV_ERROR_INVALID_PARAMS;

	devinfo = (struct gfx_bc_devinfo *) hdevice;

	if (bufno < devinfo->pvr_bcinfo.ui32BufferCount) {
		devinfo->bc_buf[bufno].pvr_sync_data = pvr_sync_data;
		*hbufferp = (IMG_HANDLE) &devinfo->bc_buf[bufno];

	} else {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR s2b_get_bc_info(IMG_HANDLE hdevice, BUFFER_INFO *bcinfop)
{
	struct gfx_bc_devinfo *devinfo = NULL;
	int rv = 0;

	if (!hdevice || !bcinfop) {
		rv = PVRSRV_ERROR_INVALID_PARAMS;
	} else {
		devinfo = (struct gfx_bc_devinfo *) hdevice;
		*bcinfop = devinfo->pvr_bcinfo;

		BCLOG("ui32BufferCount =%d",
			(int)devinfo->pvr_bcinfo.ui32BufferCount);
		BCLOG("pixelformat =%d",
			(int)devinfo->pvr_bcinfo.pixelformat);
		BCLOG("ui32Width =%d",
			(int)devinfo->pvr_bcinfo.ui32Width);
		BCLOG("ui32Height =%d",
			(int)devinfo->pvr_bcinfo.ui32Height);
		BCLOG("ui32ByteStride =%d",
			(int)devinfo->pvr_bcinfo.ui32ByteStride);
		BCLOG("ui32BufferDeviceID =%d",
			(int)devinfo->pvr_bcinfo.ui32BufferDeviceID);
		BCLOG("ui32Flags = %d",
			(int)devinfo->pvr_bcinfo.ui32Flags);

	}
	BCLOG("-%s %d (0x%x)\n", __func__, rv, (int)devinfo);
	return PVRSRV_OK;
}

static PVRSRV_ERROR s2b_get_buffer_addr(IMG_HANDLE hdevice,
					IMG_HANDLE hbuffer,
					IMG_SYS_PHYADDR **sysaddrpp,
					IMG_UINT32 *sizebytesp,
					IMG_VOID **cpuvaddrpp,
					IMG_HANDLE *osmapinfop,
					IMG_BOOL *iscontiguousp,
					IMG_UINT32 *pui32TilingStride)
{
	struct bc_buffer *bc_buf;
	PVRSRV_ERROR rv = PVRSRV_OK;
	BCLOG("+%s\n", __func__);

	if (!hdevice || !hbuffer || !sysaddrpp || !sizebytesp)
		return PVRSRV_ERROR_INVALID_PARAMS;

	bc_buf = (struct bc_buffer *)hbuffer;
	*cpuvaddrpp = NULL;
	*sizebytesp = bc_buf->size;

	if (bc_buf->paddrp) {
		*iscontiguousp = IMG_FALSE;
		*sysaddrpp = (IMG_SYS_PHYADDR *)bc_buf->paddrp;
		*osmapinfop = IMG_NULL;
		*pui32TilingStride = 0;

		BCLOG("+%s paddrp[0] 0x%x, vaddr = 0x%x, sizebytes = %d",
				__func__, (int)bc_buf->paddrp[0],
				(int)*cpuvaddrpp, (int)*sizebytesp);

	} else {
		rv = PVRSRV_ERROR_NOT_SUPPORTED;
	}
	return rv;
}

/*
 * Rest of the functions
 */
static PVRSRV_PIXEL_FORMAT v4l2_to_pvr_pixfmt(u32 v4l2pixelfmt)
{
	PVRSRV_PIXEL_FORMAT pvr_fmt;

	switch (v4l2pixelfmt) {
	case V4L2_PIX_FMT_RGB565:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_RGB565;
		break;
	case V4L2_PIX_FMT_RGB32:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_RGB888;
		break;
	case V4L2_PIX_FMT_YUYV:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_FOURCC_ORG_YUYV;
		break;
	case V4L2_PIX_FMT_UYVY:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_FOURCC_ORG_UYVY;
		break;
	case V4L2_PIX_FMT_NV12:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_NV12;
		break;
	default:
		pvr_fmt = PVRSRV_PIXEL_FORMAT_UNKNOWN;
	}
	return pvr_fmt;
}

static int gfx_bc_release_device_resources(int idx)
{
	struct gfx_bc_devinfo *devinfo;

	devinfo = g_devices[idx];
	if (devinfo == NULL)
		return -ENOENT;

	if (!devinfo->num_bufs)
		return 0;

	devinfo->num_bufs = 0;
	devinfo->pvr_bcinfo.pixelformat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
	devinfo->pvr_bcinfo.ui32Width = 0;
	devinfo->pvr_bcinfo.ui32Height = 0;
	devinfo->pvr_bcinfo.ui32ByteStride = 0;
	devinfo->pvr_bcinfo.ui32BufferDeviceID = -1;
	devinfo->pvr_bcinfo.ui32Flags = 0;
	devinfo->pvr_bcinfo.ui32BufferCount = 0;

	return 0;
}

static int gfx_bc_register(int idx)
{
	struct gfx_bc_devinfo *devinfo;
	int rv = 0;
	BCLOG("+%s\n", __func__);

	devinfo = g_devices[idx];

	if (devinfo) {
		devinfo->ref_cnt++;
		BCLOG("%s device already registered\n", __func__);
		rv = devinfo->pvr_bcinfo.ui32BufferDeviceID;
		goto end;
	}

	devinfo = (struct gfx_bc_devinfo *)
			kzalloc(sizeof(*devinfo), GFP_KERNEL);
	if (!devinfo) {
		rv = -ENOMEM;
		goto end;
	}
	BCLOG("%s devinfo idx=%d addr=0x%x\n", __func__, idx, (int)devinfo);

	devinfo->pvr_bcinfo.pixelformat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
	devinfo->pvr_bcinfo.ui32Width = 0;
	devinfo->pvr_bcinfo.ui32Height = 0;
	devinfo->pvr_bcinfo.ui32ByteStride = 0;
	devinfo->pvr_bcinfo.ui32Flags = 0;
	devinfo->pvr_bcinfo.ui32BufferCount = devinfo->num_bufs;

	devinfo->pvr_s2b_jt.ui32TableSize =
					sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE);
	devinfo->pvr_s2b_jt.pfnOpenBCDevice = s2b_open_bc_device;
	devinfo->pvr_s2b_jt.pfnCloseBCDevice = s2b_close_bc_device;
	devinfo->pvr_s2b_jt.pfnGetBCBuffer = s2b_get_bc_buffer;
	devinfo->pvr_s2b_jt.pfnGetBCInfo = s2b_get_bc_info;
	devinfo->pvr_s2b_jt.pfnGetBufferAddr = s2b_get_buffer_addr;

	if (pvr_b2s_jt.pfnPVRSRVRegisterBCDevice(&devinfo->pvr_s2b_jt,
			 &devinfo->pvr_id) != PVRSRV_OK) {
		BCLOG("RegisterBCDevice failed\n");
		rv = -EIO;
		goto end;
	}

	devinfo->pvr_bcinfo.ui32BufferDeviceID = VOUT_DEVICENODE_SUFFIX + idx;

	devinfo->ref_cnt++;
	g_devices[idx] = devinfo;
	rv = devinfo->pvr_bcinfo.ui32BufferDeviceID;
end:
	BCLOG("-%s [%d]\n", __func__, rv);
	return rv;
}

static int gfx_bc_unregister(int idx)
{
	int rv = 0;
	struct gfx_bc_devinfo *devinfo;

	devinfo = g_devices[idx];
	if (devinfo == NULL) {
		rv = -ENODEV;
		goto end;
	}

	devinfo->ref_cnt--;

	if (devinfo->ref_cnt) {
		rv = -EAGAIN;
		goto end;
	}

	if (pvr_b2s_jt.pfnPVRSRVRemoveBCDevice(devinfo->pvr_id) != PVRSRV_OK) {
		rv =  -EIO;
		goto end;
	}

	kfree(devinfo);
	g_devices[idx] = NULL;

end:
	return rv;
}

#define FIELDCOPY(dst, src, field) { (dst)->field = (src)->field; }

#define BC_BUF_PARAMS_COPY(dst, src) {		\
	FIELDCOPY(dst, src, count);			\
	FIELDCOPY(dst, src, width);			\
	FIELDCOPY(dst, src, height);		\
	FIELDCOPY(dst, src, pixel_fmt);		\
	FIELDCOPY(dst, src, stride);		\
	FIELDCOPY(dst, src, size);			\
	}

static void gfx_bc_params2_to_common(struct bc_buf_params2 *p,
			      struct bc_buf_params_common *pc)
{
	BC_BUF_PARAMS_COPY(pc, p);
}

/*
 * Validate the bc_buf_params and get the PVR pixel format
 *
 * We shouldn't need to do any further validation of the V4L2 pixelformat
 * properties as this should have been taken care of in the appropriate V4L2
 * ioctl handlers.
 */
static int gfx_bc_validateparams(
				int idx,
				struct bc_buf_params_common *p,
				struct gfx_bc_devinfo **devinfop,
				PVRSRV_PIXEL_FORMAT *pvr_pix_fmtp)
{
	struct gfx_bc_devinfo *devinfo;
	int rv = 0;

	devinfo = g_devices[idx];
	if (devinfo == NULL) {
		BCLOG("%s: no such device %d", __func__, idx);
		rv = -ENODEV;
	}

	/* validate a series of params */
	if (p->count <= 0) {
		BCLOG("%s: invalid count", __func__);
		rv = -EINVAL;
	}

	*pvr_pix_fmtp = v4l2_to_pvr_pixfmt(p->pixel_fmt);
	if (*pvr_pix_fmtp == PVRSRV_PIXEL_FORMAT_UNKNOWN) {
		BCLOG("%s: invalid pixel format", __func__);
		rv = -EINVAL;
	}

	*devinfop = rv != 0 ? NULL : devinfo;
	return rv;
}

/*
 * API for the V4L2 component
 */
int bc_init(int idx)
{
	int i, rv;

	BCLOG("+%s\n", __func__);

	if (bc_initialized == -1) {

		for (i = 0; i < DEVICE_COUNT; i++)
			g_devices[i] = NULL;

		if (!PVRGetBufferClassJTable(&pvr_b2s_jt)) {
			BCERR("no jump table to SGX APIs\n");
			rv = -EIO;
			goto end;
		}
		bc_initialized = 1;
	}

	rv = gfx_bc_register(idx);
	if (rv < 0) {
		BCERR("can't register BC service\n");
		goto end;
	}

end:
	BCLOG("-%s [%d]\n", __func__, rv);
	return rv;
}

void bc_cleanup(int idx)
{
	if (gfx_bc_release_device_resources(idx) != 0)
		BCERR("can't b/c device resources: %d\n", idx);
	if (gfx_bc_unregister(idx) != 0)
		BCERR("can't un-register BC service\n");
}

int bc_setup_complete(int idx, struct bc_buf_params2 *p)
{
	/* Fn called after successful bc_setup() so id should be valid */
	struct gfx_bc_devinfo *devinfo = g_devices[idx];
	if (p->count != devinfo->num_bufs) {
		BCLOG("+%s: Count doesn't match\n", __func__);
		return -ENODEV;
	}
	return 0;
}

int bc_setup_buffer(int idx, struct bc_buf_params2 *p, unsigned long *paddrp)
{
	int i;
	/* Fn called after successful bc_setup() so idx should be valid */
	struct gfx_bc_devinfo *devinfo = g_devices[idx];
	i = devinfo->num_bufs;
	if (unlikely(i >= VIDEO_MAX_FRAME))
		return -ENOENT;

	devinfo->num_bufs++;
	devinfo->pvr_bcinfo.ui32BufferCount = devinfo->num_bufs;

	memset(&devinfo->bc_buf[i], 0, sizeof(devinfo->bc_buf[i]));
	devinfo->bc_buf[i].paddrp = paddrp;
	devinfo->bc_buf[i].size = p->size;
	devinfo->bc_buf[i].pvr_sync_data = IMG_NULL;
	return 0;
}

int bc_setup(int idx, struct bc_buf_params2 *p)
{
	struct gfx_bc_devinfo *devinfo;
	int rv = 0;
	PVRSRV_PIXEL_FORMAT pvr_pix_fmt;
	struct bc_buf_params_common pc;

	BCLOG("+%s\n", __func__);

	gfx_bc_params2_to_common(p, &pc);
	rv = gfx_bc_validateparams(idx, &pc, &devinfo, &pvr_pix_fmt);
	if (rv != 0)
		goto end;

	p->stride = 4096; /* Tiler stride */
	p->size = p->height * p->stride;
	if (p->pixel_fmt == V4L2_PIX_FMT_NV12)
		p->size += (p->height / 2) * p->stride;	/* UV size */

	devinfo->num_bufs = 0;	/* See bc_setup_buffer */

	devinfo->pvr_bcinfo.pixelformat = pvr_pix_fmt;
	devinfo->pvr_bcinfo.ui32Width = p->width;
	devinfo->pvr_bcinfo.ui32Height = p->height;
	devinfo->pvr_bcinfo.ui32ByteStride = p->stride;
	/* I'm not 100% sure these flags are right but here goes */
	devinfo->pvr_bcinfo.ui32Flags =
					PVRSRV_BC_FLAGS_YUVCSC_FULL_RANGE |
					PVRSRV_BC_FLAGS_YUVCSC_BT601;

	BCLOG("buffers: count=%d, w=%d, h=%d, stride=%d, sz=%d fmt=%d\n",
		p->count, p->width, p->height, p->stride, p->size, pvr_pix_fmt);
end:
	BCLOG("-%s [%d]\n", __func__, rv);
	return rv;
}

/*
 * The caller of this API will ensure that the arguments are valid
 */
int bc_sync_status(int idx, int bufidx)
{
	struct gfx_bc_devinfo *devinfo = g_devices[idx];
	int ui32ReadOpsPending, ui32ReadOpsComplete;

	ui32ReadOpsPending =
		devinfo->bc_buf[bufidx].pvr_sync_data->ui32ReadOpsPending;
	ui32ReadOpsComplete =
		devinfo->bc_buf[bufidx].pvr_sync_data->ui32ReadOpsComplete;

	return ui32ReadOpsComplete == ui32ReadOpsPending ? 1 : 0;
}

