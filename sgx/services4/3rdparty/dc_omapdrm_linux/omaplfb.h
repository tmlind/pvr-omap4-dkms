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

#ifndef __OMAPLFB_H__
#define __OMAPLFB_H__

#include <linux/version.h>

#include <asm/atomic.h>

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/mutex.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/omap_drm.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define unref__ __attribute__ ((unused))

/* max number of overlays to which a framebuffer data can be direct */
#define OMAPFB_MAX_OVL_PER_FB 3

typedef void *       OMAPLFB_HANDLE;

typedef bool OMAPLFB_BOOL, *OMAPLFB_PBOOL;
#define	OMAPLFB_FALSE false
#define OMAPLFB_TRUE true

typedef	atomic_t	OMAPLFB_ATOMIC_BOOL;

typedef atomic_t	OMAPLFB_ATOMIC_INT;

typedef struct OMAPLFB_BUFFER_TAG
{
	struct OMAPLFB_BUFFER_TAG	*psNext;
	struct OMAPLFB_DEVINFO_TAG	*psDevInfo;

	struct work_struct sWork;

	IMG_SYS_PHYADDR              	sSysAddr;
	IMG_CPU_VIRTADDR             	sCPUVAddr;
	PVRSRV_SYNC_DATA            	*psSyncData;

	OMAPLFB_HANDLE      		hCmdComplete;
	unsigned long    		ulSwapInterval;

	struct drm_framebuffer *fb;

} OMAPLFB_BUFFER;

typedef struct OMAPLFB_SWAPCHAIN_TAG
{

	unsigned int			uiSwapChainID;

	unsigned long       		ulBufferCount;

	OMAPLFB_BUFFER     		*psBuffer;

	struct workqueue_struct   	*psWorkQueue;

	OMAPLFB_BOOL			bNotVSynced;

	int				iBlankEvents;

	unsigned int            	uiFBDevID;
} OMAPLFB_SWAPCHAIN;

typedef struct OMAPLFB_FBINFO_TAG
{
	unsigned long       ulFBSize;
	unsigned long       ulBufferSize;
	unsigned long       ulRoundedBufferSize;
	unsigned long       ulWidth;
	unsigned long       ulHeight;
	unsigned long       ulByteStride;
	unsigned long       ulPhysicalWidthmm;
	unsigned long       ulPhysicalHeightmm;

	IMG_SYS_PHYADDR     sSysAddr;
	IMG_CPU_VIRTADDR    sCPUVAddr;

	PVRSRV_PIXEL_FORMAT ePixelFormat;
}OMAPLFB_FBINFO;

typedef struct OMAPLFB_DEVINFO_TAG
{
	unsigned int            uiFBDevID;

	unsigned int            uiPVRDevID;

	struct mutex		sCreateSwapChainMutex;

	OMAPLFB_BUFFER          sSystemBuffer;

	PVRSRV_DC_DISP2SRV_KMJTABLE	sPVRJTable;

	PVRSRV_DC_SRV2DISP_KMJTABLE	sDCJTable;

	OMAPLFB_FBINFO          sFBInfo;

	OMAPLFB_SWAPCHAIN      *psSwapChain;

	unsigned int		uiSwapChainID;

	OMAPLFB_ATOMIC_BOOL     sFlushCommands;

	IMG_DEV_VIRTADDR	sDisplayDevVAddr;

	DISPLAY_INFO            sDisplayInfo;

	DISPLAY_FORMAT          sDisplayFormat;

	DISPLAY_DIMS            sDisplayDim;

	OMAPLFB_ATOMIC_BOOL	sBlanked;

	OMAPLFB_ATOMIC_INT	sBlankEvents;

#ifdef CONFIG_HAS_EARLYSUSPEND

	OMAPLFB_ATOMIC_BOOL	sEarlySuspendFlag;

	struct early_suspend    sEarlySuspend;
#endif

#if defined(SUPPORT_DRI_DRM)
	OMAPLFB_ATOMIC_BOOL     sLeaveVT;
#endif

	struct drm_device *dev;
    struct drm_framebuffer *system_fb;
	struct drm_framebuffer *current_fb;  /* currently displayed, system fb or flipchain buffer */

}  OMAPLFB_DEVINFO;

typedef enum _OMAPLFB_ERROR_
{
	OMAPLFB_OK                             =  0,
	OMAPLFB_ERROR_GENERIC                  =  1,
	OMAPLFB_ERROR_OUT_OF_MEMORY            =  2,
	OMAPLFB_ERROR_TOO_FEW_BUFFERS          =  3,
	OMAPLFB_ERROR_INVALID_PARAMS           =  4,
	OMAPLFB_ERROR_INIT_FAILURE             =  5,
	OMAPLFB_ERROR_CANT_REGISTER_CALLBACK   =  6,
	OMAPLFB_ERROR_INVALID_DEVICE           =  7,
	OMAPLFB_ERROR_DEVICE_REGISTER_FAILED   =  8,
	OMAPLFB_ERROR_SET_UPDATE_MODE_FAILED   =  9
} OMAPLFB_ERROR;

#define	OMAPLFB_PAGE_SIZE 4096

#ifdef	DEBUG
#define	DEBUG_PRINTK(x) printk x
#else
#define	DEBUG_PRINTK(x) printk x
#endif

#define DISPLAY_DEVICE_NAME "PowerVR OMAP Linux Display Driver"
#define	DRVNAME	"omaplfb"
#define	DEVNAME	DRVNAME
#define	DRIVER_PREFIX DRVNAME

#ifndef UNREFERENCED_PARAMETER
#define	UNREFERENCED_PARAMETER(param) (param) = (param)
#endif

OMAPLFB_ERROR OMAPLFBInit(struct drm_device *dev);
OMAPLFB_ERROR OMAPLFBDeInit(struct drm_device *dev);

OMAPLFB_DEVINFO *OMAPLFBGetDevInfoPtr(unsigned uiFBDevID);
unsigned OMAPLFBMaxFBDevIDPlusOne(void);
void *OMAPLFBAllocKernelMem(unsigned long ulSize);
void OMAPLFBFreeKernelMem(void *pvMem);
OMAPLFB_ERROR OMAPLFBGetLibFuncAddr(char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);
OMAPLFB_ERROR OMAPLFBCreateSwapQueue (OMAPLFB_SWAPCHAIN *psSwapChain);
void OMAPLFBDestroySwapQueue(OMAPLFB_SWAPCHAIN *psSwapChain);
void OMAPLFBInitBufferForSwap(OMAPLFB_BUFFER *psBuffer);
void OMAPLFBSwapHandler(OMAPLFB_BUFFER *psBuffer);
void OMAPLFBQueueBufferForSwap(OMAPLFB_SWAPCHAIN *psSwapChain, OMAPLFB_BUFFER *psBuffer);
void OMAPLFBFlip(OMAPLFB_DEVINFO *psDevInfo, OMAPLFB_BUFFER *psBuffer);
OMAPLFB_BOOL OMAPLFBWaitForVSync(OMAPLFB_DEVINFO *psDevInfo);
OMAPLFB_BOOL OMAPLFBCheckModeAndSync(OMAPLFB_DEVINFO *psDevInfo);
OMAPLFB_ERROR OMAPLFBUnblankDisplay(OMAPLFB_DEVINFO *psDevInfo);
OMAPLFB_ERROR OMAPLFBEnableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo);
OMAPLFB_ERROR OMAPLFBDisableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo);
void OMAPLFBCreateSwapChainLockInit(OMAPLFB_DEVINFO *psDevInfo);
void OMAPLFBCreateSwapChainLockDeInit(OMAPLFB_DEVINFO *psDevInfo);
void OMAPLFBCreateSwapChainLock(OMAPLFB_DEVINFO *psDevInfo);
void OMAPLFBCreateSwapChainUnLock(OMAPLFB_DEVINFO *psDevInfo);

OMAPLFB_ERROR OMAPLFBSetFb(OMAPLFB_DEVINFO *psDevInfo, struct drm_framebuffer *fb);

#endif
