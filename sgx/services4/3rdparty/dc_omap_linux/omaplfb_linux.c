/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful but, except
 * as otherwise stated in writing, without any warranty; without even the
 * implied warranty of merchantability or fitness for a particular purpose.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK
 *
 ******************************************************************************/

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#include <linux/omap_gpu.h>
#else
#include <linux/module.h>
#endif

#include <linux/version.h>
#include <linux/fb.h>
#include <asm/io.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
#include <plat/vrfb.h>
#include <plat/display.h>
#else
#include <mach/vrfb.h>
#include <mach/display.h>
#endif

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
#include <asm/cacheflush.h>
#define HOST_PAGESIZE			(4096)
#define HOST_PAGEMASK			(~(HOST_PAGESIZE-1))
#define HOST_PAGEALIGN(addr)	(((addr)+HOST_PAGESIZE-1)&HOST_PAGEMASK)
#endif

#if defined(LDM_PLATFORM)
#include <linux/platform_device.h>
#if defined(SGX_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"
#include "pvrmodule.h"

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#include "3rdparty_dc_drm_shared.h"
#endif

#if !defined(PVR_LINUX_USING_WORKQUEUES)
#error "PVR_LINUX_USING_WORKQUEUES must be defined"
#endif

MODULE_SUPPORTED_DEVICE(DEVNAME);

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
#if defined(__arm__)
static void per_cpu_cache_flush_arm(void *arg)
{
    PVR_UNREFERENCED_PARAMETER(arg);
    flush_cache_all();
}
#endif
#endif

/*
 * Kernel malloc
 * in: ui32ByteSize
 */
void *OMAPLFBAllocKernelMem(unsigned long ui32ByteSize)
{
	void *p;

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
	IMG_VOID *pvPageAlignedCPUPAddr;
	IMG_VOID *pvPageAlignedCPUVAddr;
	IMG_UINT32 ui32PageOffset;
	IMG_UINT32 ui32PageCount;
#endif
	p = kmalloc(ui32ByteSize, GFP_KERNEL);

	if(!p)
		return 0;

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
	ui32PageOffset = (IMG_UINT32) p & (HOST_PAGESIZE - 1);
	ui32PageCount = HOST_PAGEALIGN(ui32ByteSize + ui32PageOffset) / HOST_PAGESIZE;

	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINT8 *)p - ui32PageOffset);
	pvPageAlignedCPUPAddr = (IMG_VOID*) __pa(pvPageAlignedCPUVAddr);

#if defined(__arm__)
      on_each_cpu(per_cpu_cache_flush_arm, NULL, 1);
#endif
	outer_cache.flush_range((unsigned long) pvPageAlignedCPUPAddr, (unsigned long) ((pvPageAlignedCPUPAddr + HOST_PAGESIZE*ui32PageCount) - 1));
#endif
	return p;
}

/*
 * Kernel free
 * in: pvMem
 */
void OMAPLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}

/*
 * Here we get the function pointer to get jump table from
 * services using an external function.
 * in: szFunctionName
 * out: ppfnFuncTable
 */
OMAP_ERROR OMAPLFBGetLibFuncAddr (char *szFunctionName,
	PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		ERROR_PRINTK("Unable to get function pointer for %s"
			" from services", szFunctionName);
		return OMAP_ERROR_INVALID_PARAMS;
	}
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return OMAP_OK;
}

#if defined(FLIP_TECHNIQUE_FRAMEBUFFER)
/*
 * Presents the flip in the display with the framebuffer API
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBFlipNoLock(OMAPLFB_SWAPCHAIN *psSwapChain,
	unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;
	/* note: make a copy of the current var, to modify.. otherwise we
	 * fool fbdev into thinking that nothing has changed!
	 */
	struct fb_var_screeninfo var = framebuffer->var;

	/* TODO:  we really should create a separate fb for each buffer in
	 * the flip chain.. then flipping could be done either from kernel
	 * or userspace via crtc->funcs->page_flip().. this will be easier
	 * when we can more easily allocate framebuffer memory dynamically
	 * (ie. something like CMA.. http://lwn.net/Articles/416303/ ..
	 * Nicolas Pitre had some idea that highmem could make this easier
	 * because highmem pages could be more easily relocated to make
	 * contiguous memory available.. see
	 * http://lists.linaro.org/pipermail/linaro-dev/2011-February/002854.html
	 */

	/* Get the framebuffer physical address base */
	unsigned long fb_base_phyaddr =
		psDevInfo->sSystemBuffer.sSysAddr.uiAddr;

	/* Calculate the virtual Y to move in the framebuffer */
	var.yoffset = (aPhyAddr - fb_base_phyaddr) / framebuffer->fix.line_length;
	var.activate = FB_ACTIVATE_FORCE;

	/* hack.. normally virtual height would be same as actual height of the
	 * virtual framebuffer (ie. the bounding box around all sub-rects of
	 * the fb that are being scanned out to some display.. but that would
	 * cause fb_pan_display to fail.. this hack can go away once we start
	 * using crtc->funcs->page_flip()
	 */
	framebuffer->var.yres_virtual = var.yoffset + var.yres;

	fb_pan_display(framebuffer, &var);
}

void OMAPLFBFlip(OMAPLFB_SWAPCHAIN *psSwapChain, unsigned long aPhyAddr)
{
	OMAPLFBFlipNoLock(psSwapChain, aPhyAddr);
}

#elif defined(FLIP_TECHNIQUE_OVERLAY)
/*
 * Presents the flip in the display with the DSS2 overlay API
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBFlipNoLock(OMAPLFB_SWAPCHAIN *psSwapChain,
	unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info * framebuffer = psDevInfo->psLINFBInfo;
	struct omapfb_info *ofbi = FB2OFB(framebuffer);
	unsigned long fb_offset;
	int i;

	fb_offset = aPhyAddr - psDevInfo->sSystemBuffer.sSysAddr.uiAddr;

	for(i = 0; i < ofbi->num_overlays ; i++)
	{
		struct omap_dss_device *display = NULL;
		struct omap_dss_driver *driver = NULL;
		struct omap_overlay_manager *manager;
		struct omap_overlay *overlay;
		struct omap_overlay_info overlay_info;

		overlay = ofbi->overlays[i];
		manager = overlay->manager;
		overlay->get_overlay_info( overlay, &overlay_info );

		overlay_info.paddr = framebuffer->fix.smem_start + fb_offset;
		overlay_info.vaddr = framebuffer->screen_base + fb_offset;
		overlay->set_overlay_info(overlay, &overlay_info);

		if (manager) {
			display = manager->device;
			/* No display attached to this overlay, don't update */
			if (!display)
				continue;
			driver = display->driver;
			manager->apply(manager);
		}

		if (dss_ovl_manually_updated(overlay)) {
			if (driver->sched_update)
				driver->sched_update(display, 0, 0,
							overlay_info.width,
							overlay_info.height);
			else if (driver->update)
				driver->update(display, 0, 0,
							overlay_info.width,
							overlay_info.height);

		}

	}
}

void OMAPLFBFlip(OMAPLFB_SWAPCHAIN *psSwapChain, unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;
	struct omapfb_info *ofbi = FB2OFB(framebuffer);
	struct omapfb2_device *fbdev = ofbi->fbdev;

	omapfb_lock(fbdev);
	OMAPLFBFlipNoLock(psSwapChain, aPhyAddr);
	omapfb_unlock(fbdev);
}

#else
#error No flipping technique selected, please define \
	FLIP_TECHNIQUE_FRAMEBUFFER or FLIP_TECHNIQUE_OVERLAY
#endif

void OMAPLFBPresentSync(OMAPLFB_DEVINFO *psDevInfo,
			OMAPLFB_FLIP_ITEM *psFlipItem)
{
	OMAPLFBPresentSyncAddr(psDevInfo,
				(unsigned long)psFlipItem->sSysAddr->uiAddr);
}

/*
 * Present frame and synchronize with the display to prevent tearing
 * On DSI panels the sync function is used to handle FRAMEDONE IRQ
 * On DPI panels the wait_for_vsync is used to handle VSYNC IRQ
 * in: psDevInfo
 */
void OMAPLFBPresentSyncAddr(OMAPLFB_DEVINFO *psDevInfo,
	unsigned long paddr)
{
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;
	struct drm_connector *connector = NULL;
	int err = 0;

	while ((connector = omap_fbdev_get_next_connector(framebuffer, connector)))
	{
		err |= omap_connector_sync(connector);
	}

	OMAPLFBFlipNoLock(psDevInfo->psSwapChain, paddr);

	connector = NULL;
	while ((connector = omap_fbdev_get_next_connector(framebuffer, connector)))
	{
		if (err && connector->encoder)
		{
			/* if the panel didn't support sync(), then try wait_for_vsync() */
			err = omap_encoder_wait_for_vsync(connector->encoder);
		}
	}
}

#if defined(LDM_PLATFORM)

static volatile OMAP_BOOL bDeviceSuspended;

#if !defined(SUPPORT_DRI_DRM)
/*
 * Common suspend driver function
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBCommonSuspend(void)
{
	if (bDeviceSuspended)
	{
		DEBUG_PRINTK("Driver is already suspended");
		return;
	}

	OMAPLFBDriverSuspend();
	bDeviceSuspended = OMAP_TRUE;
}
#endif

#if 0
/*
 * Function called when the driver is requested to release
 * in: pDevice
 */
static void OMAPLFBDeviceRelease_Entry(struct device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver release");
	OMAPLFBCommonSuspend();
}

static struct platform_device omaplfb_device = {
	.name = DEVNAME,
	.id = -1,
	.dev = {
		.release = OMAPLFBDeviceRelease_Entry
	}
};
#endif

#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)

static struct early_suspend omaplfb_early_suspend;

/*
 * Android specific, driver is requested to be suspended
 * in: ea_event
 */
static void OMAPLFBDriverSuspend_Entry(struct early_suspend *ea_event)
{
	DEBUG_PRINTK("Requested driver suspend");
	OMAPLFBCommonSuspend();
}

/*
 * Android specific, driver is requested to be suspended
 * in: ea_event
 */
static void OMAPLFBDriverResume_Entry(struct early_suspend *ea_event)
{
	DEBUG_PRINTK("Requested driver resume");
	OMAPLFBDriverResume();
	bDeviceSuspended = OMAP_FALSE;
}

static struct platform_driver omaplfb_driver = {
	.driver = {
		.name = DRVNAME,
	}
};

#else /* defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND) */

#if !defined(SUPPORT_DRI_DRM)
/*
 * Function called when the driver is requested to be suspended
 * in: pDevice, state
 */
static int OMAPLFBDriverSuspend_Entry(struct platform_device unref__ *pDevice,
	pm_message_t unref__ state)
{
	DEBUG_PRINTK("Requested driver suspend");
	OMAPLFBCommonSuspend();
	return 0;
}

/*
 * Function called when the driver is requested to resume
 * in: pDevice
 */
static int OMAPLFBDriverResume_Entry(struct platform_device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver resume");
	OMAPLFBDriverResume();
	bDeviceSuspended = OMAP_FALSE;
	return 0;
}

/*
 * Function called when the driver is requested to shutdown
 * in: pDevice
 */
static IMG_VOID OMAPLFBDriverShutdown_Entry(
	struct platform_device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver shutdown");
	OMAPLFBCommonSuspend();
}

static struct platform_driver omaplfb_driver = {
	.driver = {
		.name = DRVNAME,
	},
	.suspend = OMAPLFBDriverSuspend_Entry,
	.resume	= OMAPLFBDriverResume_Entry,
	.shutdown = OMAPLFBDriverShutdown_Entry,
};
#endif /* !defined (SUPPORT_DRI_DRM)*/

#endif /* defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND) */

#endif /* defined(LDM_PLATFORM) */

/*
 * Driver init function
 */
#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device *dev)
#else
static int __init OMAPLFB_Init(void)
#endif
{
	if(OMAPLFBInit() != OMAP_OK)
	{
		WARNING_PRINTK("Driver init failed");
		return -ENODEV;
	}

#if defined(LDM_PLATFORM)
	DEBUG_PRINTK("Registering platform driver");
#if !defined(SUPPORT_DRI_DRM)
	if (platform_driver_register(&omaplfb_driver))
	{
		WARNING_PRINTK("Unable to register platform driver");
		if(OMAPLFBDeinit() != OMAP_OK)
			WARNING_PRINTK("Driver cleanup failed\n");
		return -ENODEV;
	}
#endif
#if 0
	DEBUG_PRINTK("Registering device driver");
	if (platform_device_register(&omaplfb_device))
	{
		WARNING_PRINTK("Unable to register platform device");
		platform_driver_unregister(&omaplfb_driver);
		if(OMAPLFBDeinit() != OMAP_OK)
			WARNING_PRINTK("Driver cleanup failed\n");
		return -ENODEV;
	}
#endif

#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)
	omaplfb_early_suspend.suspend = OMAPLFBDriverSuspend_Entry;
	omaplfb_early_suspend.resume = OMAPLFBDriverResume_Entry;
	omaplfb_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&omaplfb_early_suspend);
	DEBUG_PRINTK("Registered early suspend support");
#endif

#endif
	return 0;
}

/*
 * Driver exit function
 */
#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)
			(struct drm_device unref__ *dev)
#else
static IMG_VOID __exit OMAPLFB_Cleanup(IMG_VOID)
#endif
{
#if defined(LDM_PLATFORM)
#if 0
	DEBUG_PRINTK(format, ...)("Removing platform device");
	platform_device_unregister(&omaplfb_device);
#endif
#if !defined(SUPPORT_DRI_DRM)
	DEBUG_PRINTK("Removing platform driver");
	platform_driver_unregister(&omaplfb_driver);
#endif
#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&omaplfb_early_suspend);
#endif
#endif
	if(OMAPLFBDeinit() != OMAP_OK)
		WARNING_PRINTK("Driver cleanup failed");
}

#if !defined(SUPPORT_DRI_DRM)
late_initcall(OMAPLFB_Init);
module_exit(OMAPLFB_Cleanup);
#endif
