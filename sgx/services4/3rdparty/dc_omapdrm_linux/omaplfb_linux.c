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

#include <linux/version.h>

#include <asm/atomic.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/mutex.h>

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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
#define OMAP_DSS_DRIVER(drv, dev) struct omap_dss_driver *drv = (dev) != NULL ? (dev)->driver : NULL
#define OMAP_DSS_MANAGER(man, dev) struct omap_overlay_manager *man = (dev) != NULL ? (dev)->manager : NULL
#define	WAIT_FOR_VSYNC(man)	((man)->wait_for_vsync)
#else
#define OMAP_DSS_DRIVER(drv, dev) struct omap_dss_device *drv = (dev)
#define OMAP_DSS_MANAGER(man, dev) struct omap_dss_device *man = (dev)
#define	WAIT_FOR_VSYNC(man)	((man)->wait_vsync)
#endif

void *OMAPLFBAllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void OMAPLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}

void OMAPLFBCreateSwapChainLockInit(OMAPLFB_DEVINFO *psDevInfo)
{
	mutex_init(&psDevInfo->sCreateSwapChainMutex);
}

void OMAPLFBCreateSwapChainLockDeInit(OMAPLFB_DEVINFO *psDevInfo)
{
	mutex_destroy(&psDevInfo->sCreateSwapChainMutex);
}

void OMAPLFBCreateSwapChainLock(OMAPLFB_DEVINFO *psDevInfo)
{
	mutex_lock(&psDevInfo->sCreateSwapChainMutex);
}

void OMAPLFBCreateSwapChainUnLock(OMAPLFB_DEVINFO *psDevInfo)
{
	mutex_unlock(&psDevInfo->sCreateSwapChainMutex);
}

OMAPLFB_ERROR OMAPLFBGetLibFuncAddr (char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return (OMAPLFB_ERROR_INVALID_PARAMS);
	}

	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return (OMAPLFB_OK);
}

void OMAPLFBQueueBufferForSwap(OMAPLFB_SWAPCHAIN *psSwapChain, OMAPLFB_BUFFER *psBuffer)
{
	int res = queue_work(psSwapChain->psWorkQueue, &psBuffer->sWork);

	if (res == 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Buffer already on work queue\n",
			__FUNCTION__, psSwapChain->uiFBDevID);
	}
}

static void WorkQueueHandler(struct work_struct *psWork)
{
	OMAPLFB_BUFFER *psBuffer = container_of(psWork, OMAPLFB_BUFFER, sWork);

	OMAPLFBSwapHandler(psBuffer);
}

OMAPLFB_ERROR OMAPLFBCreateSwapQueue(OMAPLFB_SWAPCHAIN *psSwapChain)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
	psSwapChain->psWorkQueue = __create_workqueue(DEVNAME, 1, 1, 1);
#else
	psSwapChain->psWorkQueue = alloc_ordered_workqueue
			(DEVNAME, WQ_NON_REENTRANT | WQ_FREEZABLE| WQ_HIGHPRI);
#endif
	if (psSwapChain->psWorkQueue == NULL)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: create_singlethreaded_workqueue failed\n",
			__FUNCTION__, psSwapChain->uiFBDevID);

		return (OMAPLFB_ERROR_INIT_FAILURE);
	}

	return (OMAPLFB_OK);
}

void OMAPLFBInitBufferForSwap(OMAPLFB_BUFFER *psBuffer)
{
	INIT_WORK(&psBuffer->sWork, WorkQueueHandler);
}

void OMAPLFBDestroySwapQueue(OMAPLFB_SWAPCHAIN *psSwapChain)
{
	destroy_workqueue(psSwapChain->psWorkQueue);
}

void OMAPLFBFlip(OMAPLFB_DEVINFO *psDevInfo, OMAPLFB_BUFFER *psBuffer)
{
	struct drm_connector *connector = NULL;
	struct drm_framebuffer *fb = psDevInfo->current_fb;

	while ((connector = omap_framebuffer_get_next_connector(fb, connector)))
	{
		if (connector->encoder && connector->encoder->crtc)
		{
			struct drm_crtc *crtc = connector->encoder->crtc;
			if (crtc->fb == fb)
			{
				omap_crtc_page_flip(crtc, psBuffer->fb, NULL);
			}
		}
	}

	psDevInfo->current_fb = psBuffer->fb;
}

OMAPLFB_BOOL OMAPLFBWaitForVSync(OMAPLFB_DEVINFO *psDevInfo)
{
	struct drm_connector *connector = NULL;
	struct drm_framebuffer *fb = psDevInfo->current_fb;
	int err = 0;

	/* TODO: this isn't very good for virtual display w/ multiple connectors..
	 * we need to get all the current vblank counts, and then wait for them
	 * all to increment..
	 *
	 * but this is currently fine for single displays..
	 */
	while ((connector = omap_framebuffer_get_next_connector(fb, connector)))
	{
		if (connector->encoder)
		{
			err |= omap_encoder_wait_for_vsync(connector->encoder);
		}
		break;
	}

	return !err;
}

OMAPLFB_BOOL OMAPLFBCheckModeAndSync(OMAPLFB_DEVINFO *psDevInfo)
{
	struct drm_connector *connector = NULL;
	struct drm_framebuffer *fb = psDevInfo->current_fb;
	int err = 0;

	while ((connector = omap_framebuffer_get_next_connector(fb, connector)))
	{
		err |= omap_connector_sync(connector);
	}

	return !err;
}

#if 0  /* TODO */
static int OMAPLFBFrameBufferEvents(struct notifier_block *psNotif,
                             unsigned long event, void *data)
{
	OMAPLFB_DEVINFO *psDevInfo;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	struct fb_info *psFBInfo = psFBEvent->info;
	OMAPLFB_BOOL bBlanked;

	if (event != FB_EVENT_BLANK)
	{
		return 0;
	}

	bBlanked = (*(IMG_INT *)psFBEvent->data != 0) ? OMAPLFB_TRUE: OMAPLFB_FALSE;

	psDevInfo = OMAPLFBGetDevInfoPtr(psFBInfo->node);

#if 0
	if (psDevInfo != NULL)
	{
		if (bBlanked)
		{
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Blank event received\n", __FUNCTION__, psDevInfo->uiFBDevID));
		}
		else
		{
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unblank event received\n", __FUNCTION__, psDevInfo->uiFBDevID));
		}
	}
	else
	{
		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Blank/Unblank event for unknown framebuffer\n", __FUNCTION__, psFBInfo->node));
	}
#endif

	if (psDevInfo != NULL)
	{
		atomic_set(&psDevInfo->sBlanked, bBlanked);
		atomic_inc(&psDevInfo->sBlankEvents);
	}
	return 0;
}
#endif

OMAPLFB_ERROR OMAPLFBUnblankDisplay(OMAPLFB_DEVINFO *psDevInfo)
{
#if 0  /* TODO */
	int res;

	console_lock();
	res = fb_blank(psDevInfo->psLINFBInfo, 0);
	console_unlock();
	if (res != 0 && res != -EINVAL)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: fb_blank failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);
		return (OMAPLFB_ERROR_GENERIC);
	}
#endif
	return (OMAPLFB_OK);
}

#ifdef CONFIG_HAS_EARLYSUSPEND

#if 0 /* TODO currently unused. Commented to avoid compilation warnings*/
static void OMAPLFBBlankDisplay(OMAPLFB_DEVINFO *psDevInfo)
{
#if 0 /* TODO */
	console_lock();
	fb_blank(psDevInfo->psLINFBInfo, 1);
	console_unlock();
#endif
}

static void OMAPLFBEarlySuspendHandler(struct early_suspend *h)
{
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		OMAPLFB_DEVINFO *psDevInfo = OMAPLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			atomic_set(&psDevInfo->sEarlySuspendFlag, OMAPLFB_TRUE);
			OMAPLFBBlankDisplay(psDevInfo);
		}
	}
}

static void OMAPLFBEarlyResumeHandler(struct early_suspend *h)
{
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		OMAPLFB_DEVINFO *psDevInfo = OMAPLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			OMAPLFBUnblankDisplay(psDevInfo);
			atomic_set(&psDevInfo->sEarlySuspendFlag, OMAPLFB_FALSE);
		}
	}
}

#endif
#endif

OMAPLFB_ERROR OMAPLFBEnableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo)
{
#if 0 /* TODO */
	int                res;
	OMAPLFB_ERROR         eError;

	memset(&psDevInfo->sLINNotifBlock, 0, sizeof(psDevInfo->sLINNotifBlock));

	psDevInfo->sLINNotifBlock.notifier_call = OMAPLFBFrameBufferEvents;

	atomic_set(&psDevInfo->sBlanked, OMAPLFB_FALSE);
	atomic_set(&psDevInfo->sBlankEvents, 0);

	res = fb_register_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: fb_register_client failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);

		return (OMAPLFB_ERROR_GENERIC);
	}

	eError = OMAPLFBUnblankDisplay(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: UnblankDisplay failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, eError);
		return eError;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	psDevInfo->sEarlySuspend.suspend = OMAPLFBEarlySuspendHandler;
	psDevInfo->sEarlySuspend.resume = OMAPLFBEarlyResumeHandler;
	psDevInfo->sEarlySuspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	register_early_suspend(&psDevInfo->sEarlySuspend);
#endif
#endif
	return (OMAPLFB_OK);
}

OMAPLFB_ERROR OMAPLFBDisableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo)
{
#if 0 /* TODO */
	int res;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&psDevInfo->sEarlySuspend);
#endif

	res = fb_unregister_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: fb_unregister_client failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);
		return (OMAPLFB_ERROR_GENERIC);
	}

	atomic_set(&psDevInfo->sBlanked, OMAPLFB_FALSE);
#endif
	return (OMAPLFB_OK);
}

#if defined(SUPPORT_DRI_DRM) && defined(PVR_DISPLAY_CONTROLLER_DRM_IOCTL)
static OMAPLFB_DEVINFO *OMAPLFBPVRDevIDToDevInfo(unsigned uiPVRDevID)
{
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		OMAPLFB_DEVINFO *psDevInfo = OMAPLFBGetDevInfoPtr(i);

		if (psDevInfo && (psDevInfo->uiPVRDevID == uiPVRDevID))
		{
			return psDevInfo;
		}
	}

	printk(KERN_WARNING DRIVER_PREFIX
		": %s: PVR Device %u: Couldn't find device\n", __FUNCTION__, uiPVRDevID);

	return NULL;
}

int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Ioctl)(struct drm_device unref__ *dev, void *arg, struct drm_file unref__ *pFile)
{
	uint32_t *puiArgs;
	uint32_t uiCmd, uiArg;
	unsigned uiPVRDevID;
	int ret = 0;
	OMAPLFB_DEVINFO *psDevInfo;

	if (arg == NULL)
	{
		return -EFAULT;
	}

	puiArgs = (uint32_t *)arg;
	uiCmd = puiArgs[PVR_DRM_DISP_ARG_CMD];
	uiPVRDevID = puiArgs[PVR_DRM_DISP_ARG_DEV];
	uiArg = puiArgs[PVR_DRM_DISP_ARG_ARG];

	psDevInfo = OMAPLFBPVRDevIDToDevInfo(uiPVRDevID);
	if (psDevInfo == NULL)
	{
		return -EINVAL;
	}


	switch (uiCmd)
	{
		case PVR_DRM_DISP_CMD_LEAVE_VT:
		case PVR_DRM_DISP_CMD_ENTER_VT:
		{
			OMAPLFB_BOOL bLeaveVT = (uiCmd == PVR_DRM_DISP_CMD_LEAVE_VT);
			DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX ": %s: PVR Device %u: %s\n",
				__FUNCTION__, uiPVRDevID,
				bLeaveVT ? "Leave VT" : "Enter VT"));

			OMAPLFBCreateSwapChainLock(psDevInfo);

			atomic_set(&psDevInfo->sLeaveVT, bLeaveVT);
			if (psDevInfo->psSwapChain != NULL)
			{
				flush_workqueue(psDevInfo->psSwapChain->psWorkQueue);

				if (bLeaveVT)
				{
					OMAPLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);
					(void) OMAPLFBCheckModeAndSync(psDevInfo);
				}
			}

			OMAPLFBCreateSwapChainUnLock(psDevInfo);
			(void) OMAPLFBUnblankDisplay(psDevInfo);
			break;
		}
		case PVR_DRM_DISP_CMD_RESYNC:
		{
			struct drm_mode_object *obj;
			struct drm_framebuffer *fb;

			obj = drm_mode_object_find(dev, uiArg, DRM_MODE_OBJECT_FB);
			if (!obj)
			{
				printk(KERN_WARNING DRIVER_PREFIX
					": %s: Unknown FB ID%d\n", __FUNCTION__, uiArg);
				ret = -EINVAL;
				break;
			}

			fb = obj_to_fb(obj);

			if (OMAPLFBSetFb(psDevInfo, fb) != OMAPLFB_OK)
			{
				printk(KERN_WARNING DRIVER_PREFIX
					": %s: ReInit failed\n", __FUNCTION__);
				ret = -EINVAL;
				break;
			}

			break;
		}
		case PVR_DRM_DISP_CMD_ON:
		case PVR_DRM_DISP_CMD_STANDBY:
		case PVR_DRM_DISP_CMD_SUSPEND:
		case PVR_DRM_DISP_CMD_OFF:
		{
			int iFBMode;
#if defined(DEBUG)
			{
				const char *pszMode;
				switch(uiCmd)
				{
					case PVR_DRM_DISP_CMD_ON:
						pszMode = "On";
						break;
					case PVR_DRM_DISP_CMD_STANDBY:
						pszMode = "Standby";
						break;
					case PVR_DRM_DISP_CMD_SUSPEND:
						pszMode = "Suspend";
						break;
					case PVR_DRM_DISP_CMD_OFF:
						pszMode = "Off";
						break;
					default:
						pszMode = "(Unknown Mode)";
						break;
				}
				printk(KERN_WARNING DRIVER_PREFIX
					": %s: PVR Device %u: Display %s\n",
					__FUNCTION__, uiPVRDevID, pszMode);
			}
#endif
			switch(uiCmd)
			{
				case PVR_DRM_DISP_CMD_ON:
					iFBMode = FB_BLANK_UNBLANK;
					break;
				case PVR_DRM_DISP_CMD_STANDBY:
					iFBMode = FB_BLANK_HSYNC_SUSPEND;
					break;
				case PVR_DRM_DISP_CMD_SUSPEND:
					iFBMode = FB_BLANK_VSYNC_SUSPEND;
					break;
				case PVR_DRM_DISP_CMD_OFF:
					iFBMode = FB_BLANK_POWERDOWN;
					break;
				default:
					return -EINVAL;
			}

			OMAPLFBCreateSwapChainLock(psDevInfo);

			if (psDevInfo->psSwapChain != NULL)
			{
				flush_workqueue(psDevInfo->psSwapChain->psWorkQueue);
			}

#if 0 /* TODO */
			console_lock();
			ret = fb_blank(psDevInfo->psLINFBInfo, iFBMode);
			console_unlock();
#endif

			OMAPLFBCreateSwapChainUnLock(psDevInfo);

			break;
		}
		default:
		{
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}
#endif

#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device *dev)
#else
static int __init OMAPLFB_Init(void)
#endif
{

	if(OMAPLFBInit(dev) != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: OMAPLFBInit failed\n", __FUNCTION__);
		return -ENODEV;
	}

	return 0;

}

#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device *dev)
#else
static void __exit OMAPLFB_Cleanup(void)
#endif
{
	if(OMAPLFBDeInit(dev) != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: OMAPLFBDeInit failed\n", __FUNCTION__);
	}
}

#if !defined(SUPPORT_DRI_DRM)
late_initcall(OMAPLFB_Init);
module_exit(OMAPLFB_Cleanup);
#endif
