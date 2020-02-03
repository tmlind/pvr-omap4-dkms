/*************************************************************************/ /*!
@Title          PowerVR drm driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    linux module setup
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#include <linux/version.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <asm/ioctl.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,5,0))
#include <drm/drmP.h>
#else
#include <drm/drm_file.h>
#endif
#include <drm/drm.h>
#include <drm/omap_drm.h>

#include "img_defs.h"
#include "services.h"
#include "kerneldisplay.h"
#include "kernelbuffer.h"
#include "syscommon.h"
#include "pvrmmap.h"
#include "mm.h"
#include "mmap.h"
#include "mutex.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "perproc.h"
#include "handle.h"
#include "pvr_bridge_km.h"
#include "pvr_bridge.h"
#include "proc.h"
#include "pvrmodule.h"
#include "pvrversion.h"
#include "lock.h"
#include "linkage.h"
#include "pvr_drm.h"
#include "private_data.h"

#define PVR_DRM_NAME	PVRSRV_MODNAME
#define PVR_DRM_DESC	"Imagination Technologies PVR DRM"
#define PVR_DRM_DATE	"20110701"

static struct drm_driver sPVRDrmDriver;
struct platform_device *gpsPVRLDMDev;
static struct drm_device *gpsPVRDRMDev;

#define PVR_DRM_FILE struct drm_file *

static int pvr_drm_load(struct drm_device *dev, unsigned long flags)
{
	dev_info(dev->dev, "%s\n", __func__);
	gpsPVRDRMDev = dev;
	gpsPVRLDMDev = to_platform_device(dev->dev);

	return PVRCore_Init();
}

static int pvr_drm_unload(struct drm_device *dev)
{
	dev_info(dev->dev, "%s\n", __func__);
	PVRCore_Cleanup();

	return 0;
}

static int pvr_open(struct drm_device *dev, struct drm_file *file)
{
	dev_info(dev->dev, "%s\n", __func__);

	return PVRSRVOpen(dev, file);
}

static int pvr_drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	void *priv = get_private(file_priv);
	int error;

	error = drm_release(inode, filp);
	if (error)
		pr_warn("%s: drm_release failed: %i\n", __func__, error);

	PVRSRVRelease(priv);

	return 0;
}

static int pvr_ioctl_command(struct drm_device *dev, void *arg, struct drm_file *filp)
{
	dev_info(dev->dev, "%s: dev: %px arg: %px filp: %px\n", __func__, dev, arg, filp);

	return PVRSRV_BridgeDispatchKM(dev, arg, filp);
}

static int pvr_ioctl_drm_is_master(struct drm_device *dev, void *arg, struct drm_file *filp)
{
	dev_info(dev->dev, "%s: dev: %px arg: %px filp: %px\n", __func__, dev, arg, filp);

	return 0;
}

static int pvr_ioctl_unpriv(struct drm_device *dev, void *arg, struct drm_file *filp)
{
	dev_info(dev->dev, "%s: dev: %px arg: %px filp: %px\n", __func__, dev, arg, filp);

	return 0;
}

static int pvr_ioctl_dbgdrv(struct drm_device *dev, void *arg, struct drm_file *filp)
{
	dev_info(dev->dev, "%s: dev: %px arg: %px filp: %px\n", __func__, dev, arg, filp);

	return 0;
}

static struct drm_ioctl_desc pvr_ioctls[] = {
        DRM_IOCTL_DEF_DRV(PVR_SRVKM, pvr_ioctl_command,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
        DRM_IOCTL_DEF_DRV(PVR_IS_MASTER, pvr_ioctl_drm_is_master,
			  DRM_RENDER_ALLOW | DRM_MASTER | DRM_UNLOCKED),
        DRM_IOCTL_DEF_DRV(PVR_UNPRIV, pvr_ioctl_unpriv,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
        DRM_IOCTL_DEF_DRV(PVR_DBGDRV, pvr_ioctl_dbgdrv,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
        DRM_IOCTL_DEF_DRV(PVR_DISP, drm_invalid_op,
			  DRM_MASTER | DRM_UNLOCKED)
};

#if defined(SUPPORT_DRI_DRM_EXTERNAL)
int pvr_ioctl_base;
static struct omap_drm_plugin plugin = {
		.name = PVR_DRM_NAME,
		.open = pvr_open,
		////.load = pvr_drm_load,
		////.unload = pvr_drm_unload,
		////.release = pvr_drm_release,
		.ioctls = pvr_ioctls,
		.num_ioctls = ARRAY_SIZE(pvr_ioctls),
		.ioctl_base = 0,	/* set by omap_drv */
};

static int prv_quirk_omap4_init(struct drm_device *dev)
{
	int error;

	plugin.dev = dev;

	error = omap_drm_register_plugin(&plugin);
	if (error) {
		pr_err("%s: omap_drm_register_plugin failed: %i", __func__, error);
	}

	return error;
}

static void pvr_quirk_omap4_cleanup(void)
{
	int error;

	error = omap_drm_unregister_plugin(&plugin);
	if (error)
		pr_err("%s: failed: %i\n", __func__, error);
}

#else
static inline int pvr_quirk_omap4_init(struct drm_device *dev)
{
	return 0;
}
static inline void pvr_quirk_omap4_cleanup(void)
{
}
#endif

static const struct file_operations sPVRFileOps =
{
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = pvr_drm_release,
	.mmap = PVRMMap,
	.poll = drm_poll,
};

static struct drm_driver sPVRDrmDriver = {
	.driver_features = DRIVER_RENDER,
	.dev_priv_size = 0,
	.open = pvr_open,
	.ioctls = pvr_ioctls,
	.fops = &sPVRFileOps,
	.name = "pvr",
	.desc = PVR_DRM_DESC,
	.date = PVR_DRM_DATE,
	.major = PVRVERSION_MAJ,
	.minor = PVRVERSION_MIN,
	.patchlevel = PVRVERSION_BUILD,
};

static struct of_device_id pvr_ids[] = {
	{
		.compatible = "ti,omap4-sgx540-120",
	},
	{
		.compatible = "ti,omap-omap4-sgx544-112",
	},
	{
		.compatible = SYS_SGX_DEV_NAME
	},
	{}
};
MODULE_DEVICE_TABLE(of, pvr_ids);

static int pvr_probe(struct platform_device *pdev)
{
	struct drm_device *ddev;
	int error;

	ddev = drm_dev_alloc(&sPVRDrmDriver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	error = pvr_drm_load(ddev, 0);
	if (error)
		return error;

	error = drm_dev_register(ddev, 0);
	if (error < 0) {
		pvr_drm_unload(gpsPVRDRMDev);

		return error;
	}

	gpsPVRLDMDev = pdev;

	error = prv_quirk_omap4_init(ddev);
	if (error)
		return error;

	return 0;
}

static int pvr_remove(struct platform_device *pDevice)
{
	pvr_quirk_omap4_cleanup();
	drm_put_dev(gpsPVRDRMDev);
	pvr_drm_unload(gpsPVRDRMDev);
	gpsPVRDRMDev = NULL;

	return 0;
}

static struct platform_driver pvr_driver = {
	.driver = {
		.name = PVR_DRM_NAME,
		.of_match_table = pvr_ids,
	},
	.probe = pvr_probe,
	.remove = pvr_remove,
	.suspend = PVRSRVDriverSuspend,
	.resume = PVRSRVDriverResume,
	.shutdown = PVRSRVDriverShutdown,
};

static int __init pvr_init(void)
{
	/* Must come before attempting to print anything via Services */
	PVRDPFInit();

	return platform_driver_register(&pvr_driver);
}

static void __exit pvr_exit(void)
{
	platform_driver_unregister(&pvr_driver);
}

module_init(pvr_init);
module_exit(pvr_exit);


