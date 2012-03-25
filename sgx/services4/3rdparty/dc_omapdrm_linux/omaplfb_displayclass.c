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
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"

#define	OMAPLFB_COMMAND_COUNT		1

#define	OMAPLFB_VSYNC_SETTLE_COUNT	5

#define	OMAPLFB_MAX_NUM_DEVICES		FB_MAX
#if (OMAPLFB_MAX_NUM_DEVICES > FB_MAX)
#error "OMAPLFB_MAX_NUM_DEVICES must not be greater than FB_MAX"
#endif

static OMAPLFB_DEVINFO *gapsDevInfo[OMAPLFB_MAX_NUM_DEVICES];

static PFN_DC_GET_PVRJTABLE gpfnGetPVRJTable = NULL;

static inline unsigned long RoundUpToMultiple(unsigned long x, unsigned long y)
{
	unsigned long div = x / y;
	unsigned long rem = x % y;

	return (div + ((rem == 0) ? 0 : 1)) * y;
}

static unsigned long GCD(unsigned long x, unsigned long y)
{
	while (y != 0)
	{
		unsigned long r = x % y;
		x = y;
		y = r;
	}

	return x;
}

static unsigned long LCM(unsigned long x, unsigned long y)
{
	unsigned long gcd = GCD(x, y);

	return (gcd == 0) ? 0 : ((x / gcd) * y);
}

unsigned OMAPLFBMaxFBDevIDPlusOne(void)
{
	return OMAPLFB_MAX_NUM_DEVICES;
}

OMAPLFB_DEVINFO *OMAPLFBGetDevInfoPtr(unsigned uiFBDevID)
{
	WARN_ON(uiFBDevID >= OMAPLFBMaxFBDevIDPlusOne());

	if (uiFBDevID >= OMAPLFB_MAX_NUM_DEVICES)
	{
		return NULL;
	}

	return gapsDevInfo[uiFBDevID];
}

static inline void OMAPLFBSetDevInfoPtr(unsigned uiFBDevID, OMAPLFB_DEVINFO *psDevInfo)
{
	WARN_ON(uiFBDevID >= OMAPLFB_MAX_NUM_DEVICES);

	if (uiFBDevID < OMAPLFB_MAX_NUM_DEVICES)
	{
		gapsDevInfo[uiFBDevID] = psDevInfo;
	}
}

static inline OMAPLFB_BOOL SwapChainHasChanged(OMAPLFB_DEVINFO *psDevInfo, OMAPLFB_SWAPCHAIN *psSwapChain)
{
	return (psDevInfo->psSwapChain != psSwapChain) ||
		(psDevInfo->uiSwapChainID != psSwapChain->uiSwapChainID);
}

static inline OMAPLFB_BOOL DontWaitForVSync(OMAPLFB_DEVINFO *psDevInfo)
{
	OMAPLFB_BOOL bDontWait;

	bDontWait = atomic_read(&psDevInfo->sBlanked) ||
			atomic_read(&psDevInfo->sFlushCommands);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	bDontWait = bDontWait || atomic_read(&psDevInfo->sEarlySuspendFlag);
#endif
#if defined(SUPPORT_DRI_DRM)
	bDontWait = bDontWait || atomic_read(&psDevInfo->sLeaveVT);
#endif
	return bDontWait;
}

static IMG_VOID SetDCState(IMG_HANDLE hDevice, IMG_UINT32 ui32State)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)hDevice;

	switch (ui32State)
	{
		case DC_STATE_FLUSH_COMMANDS:
		    atomic_set(&psDevInfo->sFlushCommands, OMAPLFB_TRUE);
			break;
		case DC_STATE_NO_FLUSH_COMMANDS:
		    atomic_set(&psDevInfo->sFlushCommands, OMAPLFB_FALSE);
			break;
		default:
			break;
	}
}

static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 uiPVRDevID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_ERROR eError;
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i = 0; i < uiMaxFBDevIDPlusOne; i++)
	{
		psDevInfo = OMAPLFBGetDevInfoPtr(i);
		if (psDevInfo != NULL && psDevInfo->uiPVRDevID == uiPVRDevID)
		{
			break;
		}
	}
	if (i == uiMaxFBDevIDPlusOne)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: PVR Device %u not found\n", __FUNCTION__, uiPVRDevID));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;

	eError = OMAPLFBUnblankDisplay(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: OMAPLFBUnblankDisplay failed (%d)\n",
			__FUNCTION__, psDevInfo->uiFBDevID, eError));
		return PVRSRV_ERROR_UNBLANK_DISPLAY_FAILED;
	}

	*phDevice = (IMG_HANDLE)psDevInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
#if defined(SUPPORT_DRI_DRM)
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)hDevice;

	atomic_set(&psDevInfo->sLeaveVT, OMAPLFB_FALSE);
	(void) OMAPLFBUnblankDisplay(psDevInfo);
#else
	UNREFERENCED_PARAMETER(hDevice);
#endif
	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
                                  IMG_UINT32 *pui32NumFormats,
                                  DISPLAY_FORMAT *psFormat)
{
	OMAPLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !pui32NumFormats)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*pui32NumFormats = 1;

	if(psFormat)
	{
		psFormat[0] = psDevInfo->sDisplayFormat;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice,
                               DISPLAY_FORMAT *psFormat,
                               IMG_UINT32 *pui32NumDims,
                               DISPLAY_DIMS *psDim)
{
	OMAPLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*pui32NumDims = 1;

	if(psDim)
	{
		psDim[0] = psDevInfo->sDisplayDim;
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	OMAPLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;

	return PVRSRV_OK;
}


static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	OMAPLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psDCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE        hDevice,
                                    IMG_HANDLE        hBuffer,
                                    IMG_SYS_PHYADDR   **ppsSysAddr,
                                    IMG_UINT32        *pui32ByteSize,
                                    IMG_VOID          **ppvCpuVAddr,
                                    IMG_HANDLE        *phOSMapInfo,
                                    IMG_BOOL          *pbIsContiguous,
                                    IMG_UINT32	      *pui32TilingStride)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_BUFFER *psSystemBuffer;

	UNREFERENCED_PARAMETER(pui32TilingStride);

	if(!hDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(!hBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!ppsSysAddr)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!pui32ByteSize)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	psSystemBuffer = (OMAPLFB_BUFFER *)hBuffer;

	*ppsSysAddr = &psSystemBuffer->sSysAddr;

	*pui32ByteSize = (IMG_UINT32)psDevInfo->sFBInfo.ulBufferSize;

	if (ppvCpuVAddr)
	{
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;
	}

	if (phOSMapInfo)
	{
		*phOSMapInfo = (IMG_HANDLE)0;
	}

	if (pbIsContiguous)
	{
		*pbIsContiguous = IMG_TRUE;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
                                      IMG_UINT32 ui32Flags,
                                      DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
                                      DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
                                      IMG_UINT32 ui32BufferCount,
                                      PVRSRV_SYNC_DATA **ppsSyncData,
                                      IMG_UINT32 ui32OEMFlags,
                                      IMG_HANDLE *phSwapChain,
                                      IMG_UINT32 *pui32SwapChainID)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_BUFFER *psBuffer;
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	struct drm_mode_fb_cmd mode_cmd;
	void *vaddr;
	unsigned long paddr;

	UNREFERENCED_PARAMETER(ui32OEMFlags);

	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	OMAPLFBCreateSwapChainLock(psDevInfo);

	if(psDevInfo->psSwapChain != NULL)
	{
		eError = PVRSRV_ERROR_FLIP_CHAIN_EXISTS;
		goto ExitUnLock;
	}

	if(ui32BufferCount > psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers)
	{
		eError = PVRSRV_ERROR_TOOMANYBUFFERS;
		goto ExitUnLock;
	}

	if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnLock;
	}

	UNREFERENCED_PARAMETER(ui32Flags);

	psSwapChain = (OMAPLFB_SWAPCHAIN*)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_SWAPCHAIN));
	if(!psSwapChain)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ExitUnLock;
	}

	psBuffer = (OMAPLFB_BUFFER*)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_BUFFER) * ui32BufferCount);
	if(!psBuffer)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeSwapChain;
	}

	psSwapChain->ulBufferCount = 0;
	psSwapChain->psBuffer = psBuffer;
	psSwapChain->bNotVSynced = OMAPLFB_TRUE;
	psSwapChain->uiFBDevID = psDevInfo->uiFBDevID;

	mode_cmd.pitch = psDstSurfAttrib->sDims.ui32ByteStride;
	mode_cmd.width = psDstSurfAttrib->sDims.ui32Width;
	mode_cmd.height = psDstSurfAttrib->sDims.ui32Height;
	mode_cmd.bpp = psDevInfo->system_fb->bits_per_pixel;
	mode_cmd.depth = psDevInfo->system_fb->depth;

	for (i = 0; i < ui32BufferCount; i++)
	{
		int screen_width;

		psBuffer[i].psNext = &psBuffer[0];

		psBuffer[i].fb = omap_framebuffer_init(psDevInfo->dev, &mode_cmd);

		if (!psBuffer[i].fb)
		{
			ui32BufferCount = i;
			break;
		}

		if (i > 0)
			psBuffer[i-1].psNext = &psBuffer[i];

		psBuffer[i].psSyncData = ppsSyncData[i];

		omap_framebuffer_get_buffer(psBuffer[i].fb, 0, 0, &vaddr, &paddr, &screen_width);

		psBuffer[i].sCPUVAddr = vaddr;
		psBuffer[i].sSysAddr.uiAddr = paddr;
		psBuffer[i].psDevInfo = psDevInfo;

		OMAPLFBInitBufferForSwap(&psBuffer[i]);
	}

	psSwapChain->ulBufferCount = i;

	if (OMAPLFBCreateSwapQueue(psSwapChain) != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Failed to create workqueue\n",
			__FUNCTION__, psDevInfo->uiFBDevID);
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto ErrorFreeBuffers;
	}

	if (OMAPLFBEnableLFBEventNotification(psDevInfo)!= OMAPLFB_OK)
	{
		eError = PVRSRV_ERROR_UNABLE_TO_ENABLE_EVENT;
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Couldn't enable framebuffer event notification\n",
			__FUNCTION__, psDevInfo->uiFBDevID);
		goto ErrorDestroySwapQueue;
	}

	psDevInfo->uiSwapChainID++;
	if (psDevInfo->uiSwapChainID == 0)
	{
		psDevInfo->uiSwapChainID++;
	}

	psSwapChain->uiSwapChainID = psDevInfo->uiSwapChainID;

	psDevInfo->psSwapChain = psSwapChain;

	*pui32SwapChainID = psDevInfo->uiSwapChainID;

	*phSwapChain = (IMG_HANDLE)psSwapChain;

	eError = PVRSRV_OK;
	goto ExitUnLock;

ErrorDestroySwapQueue:
	OMAPLFBDestroySwapQueue(psSwapChain);
ErrorFreeBuffers:
	OMAPLFBFreeKernelMem(psBuffer);
ErrorFreeSwapChain:
	OMAPLFBFreeKernelMem(psSwapChain);
ExitUnLock:
	OMAPLFBCreateSwapChainUnLock(psDevInfo);
	return eError;
}

static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_ERROR eError;
	int i;

	if(!hDevice || !hSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN*)hSwapChain;

	OMAPLFBCreateSwapChainLock(psDevInfo);

	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Swap chain mismatch\n",
			__FUNCTION__, psDevInfo->uiFBDevID);

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnLock;
	}

	OMAPLFBDestroySwapQueue(psSwapChain);

	eError = OMAPLFBDisableLFBEventNotification(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Couldn't disable framebuffer event notification\n",
			__FUNCTION__, psDevInfo->uiFBDevID);
	}

	for (i = 0; i < psSwapChain->ulBufferCount; i++)
	{
		struct drm_framebuffer *fb = psSwapChain->psBuffer[i].fb;
		fb->funcs->destroy(fb);
	}

	OMAPLFBFreeKernelMem(psSwapChain->psBuffer);
	OMAPLFBFreeKernelMem(psSwapChain);

	psDevInfo->psSwapChain = NULL;

	OMAPLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);
	(void) OMAPLFBCheckModeAndSync(psDevInfo);

	eError = PVRSRV_OK;

ExitUnLock:
	OMAPLFBCreateSwapChainUnLock(psDevInfo);

	return eError;
}

static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain,
	IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_UINT32 *pui32BufferCount,
                                 IMG_HANDLE *phBuffer)
{
	OMAPLFB_DEVINFO   *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	PVRSRV_ERROR eError;
	unsigned i;

	if(!hDevice
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN*)hSwapChain;

	OMAPLFBCreateSwapChainLock(psDevInfo);

	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Swap chain mismatch\n",
			__FUNCTION__, psDevInfo->uiFBDevID);

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	*pui32BufferCount = (IMG_UINT32)psSwapChain->ulBufferCount;

	for(i=0; i<psSwapChain->ulBufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}

	eError = PVRSRV_OK;

Exit:
	OMAPLFBCreateSwapChainUnLock(psDevInfo);

	return eError;
}

static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
                                   IMG_HANDLE hBuffer,
                                   IMG_UINT32 ui32SwapInterval,
                                   IMG_HANDLE hPrivateTag,
                                   IMG_UINT32 ui32ClipRectCount,
                                   IMG_RECT *psClipRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hBuffer);
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(ui32ClipRectCount);
	UNREFERENCED_PARAMETER(psClipRect);

	return PVRSRV_OK;
}

static PVRSRV_ERROR SwapToDCSystem(IMG_HANDLE hDevice,
                                   IMG_HANDLE hSwapChain)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);

	return PVRSRV_OK;
}

static OMAPLFB_BOOL WaitForVSyncSettle(OMAPLFB_DEVINFO *psDevInfo)
{
		unsigned i;
		for(i = 0; i < OMAPLFB_VSYNC_SETTLE_COUNT; i++)
		{
			if (DontWaitForVSync(psDevInfo) || !OMAPLFBWaitForVSync(psDevInfo))
			{
				return OMAPLFB_FALSE;
			}
		}

		return OMAPLFB_TRUE;
}

void OMAPLFBSwapHandler(OMAPLFB_BUFFER *psBuffer)
{
	OMAPLFB_DEVINFO *psDevInfo = psBuffer->psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;
	OMAPLFB_BOOL bPreviouslyNotVSynced;

#if defined(SUPPORT_DRI_DRM)
	if (!atomic_read(&psDevInfo->sLeaveVT))
#endif
	{
		OMAPLFBFlip(psDevInfo, psBuffer);
	}

	bPreviouslyNotVSynced = psSwapChain->bNotVSynced;
	psSwapChain->bNotVSynced = OMAPLFB_TRUE;


	if (!DontWaitForVSync(psDevInfo))
	{
		int iBlankEvents = atomic_read(&psDevInfo->sBlankEvents);

		(void) OMAPLFBCheckModeAndSync(psDevInfo);

		psSwapChain->bNotVSynced = OMAPLFB_FALSE;

		if (bPreviouslyNotVSynced || psSwapChain->iBlankEvents != iBlankEvents)
		{
			psSwapChain->iBlankEvents = iBlankEvents;
			psSwapChain->bNotVSynced = !WaitForVSyncSettle(psDevInfo);
		}
		else if (psBuffer->ulSwapInterval != 0)
		{
			psSwapChain->bNotVSynced = !OMAPLFBWaitForVSync(psDevInfo);
		}
	}

	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psBuffer->hCmdComplete, IMG_TRUE);
}

static IMG_BOOL ProcessFlip(IMG_HANDLE  hCmdCookie,
                            IMG_UINT32  ui32DataSize,
                            IMG_VOID   *pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_BUFFER *psBuffer;
	OMAPLFB_SWAPCHAIN *psSwapChain;

	if(!hCmdCookie || !pvData)
	{
		return IMG_FALSE;
	}

	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;

	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return IMG_FALSE;
	}

	psDevInfo = (OMAPLFB_DEVINFO*)psFlipCmd->hExtDevice;
	psBuffer = (OMAPLFB_BUFFER*)psFlipCmd->hExtBuffer;
	psSwapChain = (OMAPLFB_SWAPCHAIN*) psFlipCmd->hExtSwapChain;

	OMAPLFBCreateSwapChainLock(psDevInfo);

	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: Device %u (PVR Device ID %u): The swap chain has been destroyed\n",
			__FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID));
	}
	else
	{
		psBuffer->hCmdComplete = (OMAPLFB_HANDLE)hCmdCookie;
		psBuffer->ulSwapInterval = (unsigned long)psFlipCmd->ui32SwapInterval;

		OMAPLFBQueueBufferForSwap(psSwapChain, psBuffer);
	}

	OMAPLFBCreateSwapChainUnLock(psDevInfo);

	return IMG_TRUE;
}


static OMAPLFB_ERROR OMAPLFBInitFBDev(OMAPLFB_DEVINFO *psDevInfo)
{
	struct drm_framebuffer *fb = psDevInfo->system_fb;
	OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	unsigned long FBSize;
	unsigned long ulLCM;
	unsigned uiFBDevID = psDevInfo->uiFBDevID;
	void *vaddr;
	unsigned long paddr;
	int screen_width, line_length;

	FBSize = fb->height * fb->pitch;

	omap_framebuffer_get_buffer(fb, 0, 0, &vaddr, &paddr, &screen_width);

	line_length = screen_width * fb->bits_per_pixel / 8;

	if (FBSize == 0 || line_length == 0)
	{
		return OMAPLFB_ERROR_INVALID_DEVICE;
	}

	// XXX todo hold reference to drm, fb.. so it isn't unloaded/freed
	// beneath us


	ulLCM = LCM(line_length, OMAPLFB_PAGE_SIZE);

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer physical address: 0x%lx\n",
			psDevInfo->uiFBDevID, paddr));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual address: 0x%lx\n",
			psDevInfo->uiFBDevID, (unsigned long)vaddr));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer size: %lu\n",
			psDevInfo->uiFBDevID, FBSize));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer width: %u\n",
			psDevInfo->uiFBDevID, fb->width));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer height: %u\n",
			psDevInfo->uiFBDevID, fb->height));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer stride: %u\n",
			psDevInfo->uiFBDevID, fb->pitch));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: LCM of stride and page size: %lu\n",
			psDevInfo->uiFBDevID, ulLCM));

	psPVRFBInfo->sSysAddr.uiAddr = paddr;
	psPVRFBInfo->sCPUVAddr = vaddr;

	psPVRFBInfo->ulWidth = fb->width;
	psPVRFBInfo->ulHeight = fb->height;
	psPVRFBInfo->ulByteStride =  fb->pitch;
	psPVRFBInfo->ulFBSize = FBSize;
	psPVRFBInfo->ulBufferSize = FBSize;

	psPVRFBInfo->ulRoundedBufferSize = RoundUpToMultiple(psPVRFBInfo->ulBufferSize, ulLCM);

	/* TODO: we don't really have enough info to know the actual color format */
	if(fb->bits_per_pixel == 16)
	{
		psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
	}
	else if(fb->bits_per_pixel == 32)
	{
		psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
	}
	else
	{
		printk(KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unknown FB format\n",
			__FUNCTION__, uiFBDevID);
	}

	/* TODO: the framebuffer may be virtual so physical sizes do not make
	 * sense here.. instead userspace should get this information (if needed)
	 * from the individual drm_connector's..
	psDevInfo->sFBInfo.ulPhysicalWidthmm =
		((int)psLINFBInfo->var.width  > 0) ? psLINFBInfo->var.width  : 90;

	psDevInfo->sFBInfo.ulPhysicalHeightmm =
		((int)psLINFBInfo->var.height > 0) ? psLINFBInfo->var.height : 54;
	 */

	psDevInfo->sFBInfo.sSysAddr.uiAddr = psPVRFBInfo->sSysAddr.uiAddr;
	psDevInfo->sFBInfo.sCPUVAddr = psPVRFBInfo->sCPUVAddr;

	return OMAPLFB_OK;
}

/* perform the minimal setup that is re-performed when fb dimensions change,
 * such as calculation of # of flip chain buffers
 */
OMAPLFB_ERROR OMAPLFBSetFb(OMAPLFB_DEVINFO *psDevInfo,
        struct drm_framebuffer *fb)
{
	OMAPLFB_ERROR err;

	psDevInfo->system_fb = psDevInfo->current_fb = fb;

	err = OMAPLFBInitFBDev(psDevInfo);
	if (err != OMAPLFB_OK)
	{
		printk(KERN_INFO DRIVER_PREFIX
			 ": %s: Could not reinit device\n", __FUNCTION__);
		return err;
	}

	/* we don't actually know how many buffers we can create.. so this is arbitrary:
	 */
	psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = 3;
	psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1;
	psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 1;

	psDevInfo->sDisplayInfo.ui32PhysicalWidthmm = psDevInfo->sFBInfo.ulPhysicalWidthmm;
	psDevInfo->sDisplayInfo.ui32PhysicalHeightmm = psDevInfo->sFBInfo.ulPhysicalHeightmm;

	strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);

	psDevInfo->sDisplayFormat.pixelformat = psDevInfo->sFBInfo.ePixelFormat;
	psDevInfo->sDisplayDim.ui32Width      = (IMG_UINT32)psDevInfo->sFBInfo.ulWidth;
	psDevInfo->sDisplayDim.ui32Height     = (IMG_UINT32)psDevInfo->sFBInfo.ulHeight;
	psDevInfo->sDisplayDim.ui32ByteStride = (IMG_UINT32)psDevInfo->sFBInfo.ulByteStride;

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Maximum number of swap chain buffers: %u\n",
			psDevInfo->uiFBDevID, psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers));


	psDevInfo->sSystemBuffer.fb = fb;
	psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
	psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psDevInfo->sSystemBuffer.psDevInfo = psDevInfo;

	return OMAPLFB_OK;
}

static void OMAPLFBDeInitFBDev(OMAPLFB_DEVINFO *psDevInfo)
{
    /* TODO put/get the drm device to ensure it isn't unloaded under us */
#if 0
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	console_lock();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL)
	{
		(void) psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	console_unlock();
#endif
}

static OMAPLFB_DEVINFO *OMAPLFBInitDev(unsigned uiFBDevID, struct drm_device *dev)
{
	PFN_CMD_PROC	 	pfnCmdProcList[OMAPLFB_COMMAND_COUNT];
	IMG_UINT32		aui32SyncCountList[OMAPLFB_COMMAND_COUNT][2];
	OMAPLFB_DEVINFO		*psDevInfo = NULL;

	psDevInfo = (OMAPLFB_DEVINFO *)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_DEVINFO));

	if(psDevInfo == NULL)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: Couldn't allocate device information structure\n",
			__FUNCTION__, uiFBDevID);

		goto ErrorExit;
	}

	memset(psDevInfo, 0, sizeof(OMAPLFB_DEVINFO));

	psDevInfo->uiFBDevID = uiFBDevID;
	psDevInfo->dev = dev;

	if(!(*gpfnGetPVRJTable)(&psDevInfo->sPVRJTable))
	{
		goto ErrorFreeDevInfo;
	}

	if(OMAPLFBSetFb(psDevInfo, omap_drm_get_default_fb(dev)) != OMAPLFB_OK)
	{
		goto ErrorFreeDevInfo;
	}

    OMAPLFBInitBufferForSwap(&psDevInfo->sSystemBuffer);

	psDevInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
	psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
	psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
	psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
	psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
	psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
	psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
	psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
	psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
	psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
	psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
	psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
	psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
	psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
	psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
	psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
	psDevInfo->sDCJTable.pfnSwapToDCSystem = SwapToDCSystem;
	psDevInfo->sDCJTable.pfnSetDCState = SetDCState;

	if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice(
		&psDevInfo->sDCJTable,
		&psDevInfo->uiPVRDevID,
		NULL) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Services device registration failed\n",
			__FUNCTION__, uiFBDevID);

		goto ErrorDeInitFBDev;
	}
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		": Device %u: PVR Device ID: %u\n",
		psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID));

	pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;

	aui32SyncCountList[DC_FLIP_COMMAND][0] = 0;
	aui32SyncCountList[DC_FLIP_COMMAND][1] = 2;

	if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList(psDevInfo->uiPVRDevID,
								&pfnCmdProcList[0],
								aui32SyncCountList,
								OMAPLFB_COMMAND_COUNT) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: Couldn't register command "
			"processing functions with PVR Services\n",
			__FUNCTION__, uiFBDevID);
		goto ErrorUnregisterDevice;
	}

	OMAPLFBCreateSwapChainLockInit(psDevInfo);

	atomic_set(&psDevInfo->sBlanked, OMAPLFB_FALSE);
	atomic_set(&psDevInfo->sBlankEvents, 0);
	atomic_set(&psDevInfo->sFlushCommands, OMAPLFB_FALSE);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	atomic_set(&psDevInfo->sEarlySuspendFlag, OMAPLFB_FALSE);
#endif
#if defined(SUPPORT_DRI_DRM)
	atomic_set(&psDevInfo->sLeaveVT, OMAPLFB_FALSE);
#endif
	return psDevInfo;

ErrorUnregisterDevice:
	(void)psDevInfo->sPVRJTable.pfnPVRSRVRemoveDCDevice(psDevInfo->uiPVRDevID);
ErrorDeInitFBDev:
	OMAPLFBDeInitFBDev(psDevInfo);
ErrorFreeDevInfo:
	OMAPLFBFreeKernelMem(psDevInfo);
ErrorExit:
	return NULL;
}

OMAPLFB_ERROR OMAPLFBInit(struct drm_device *dev)
{
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;
	unsigned uiDevicesFound = 0;

	if(OMAPLFBGetLibFuncAddr ("PVRGetDisplayClassJTable", &gpfnGetPVRJTable) != OMAPLFB_OK)
	{
		return OMAPLFB_ERROR_INIT_FAILURE;
	}

	for(i = uiMaxFBDevIDPlusOne; i-- != 0;)
	{
		OMAPLFB_DEVINFO *psDevInfo = OMAPLFBGetDevInfoPtr(i);

		if (psDevInfo)
		{
			/* slot already taken */
			continue;
		}

		psDevInfo = OMAPLFBInitDev(i, dev);

		if (psDevInfo != NULL)
		{
			OMAPLFBSetDevInfoPtr(i, psDevInfo);
			uiDevicesFound++;
		}

		break;
	}

	return (uiDevicesFound != 0) ? OMAPLFB_OK : OMAPLFB_ERROR_INIT_FAILURE;
}

static OMAPLFB_BOOL OMAPLFBDeInitDev(OMAPLFB_DEVINFO *psDevInfo)
{
	PVRSRV_DC_DISP2SRV_KMJTABLE *psPVRJTable = &psDevInfo->sPVRJTable;

	OMAPLFBCreateSwapChainLockDeInit(psDevInfo);

	psPVRJTable = &psDevInfo->sPVRJTable;

	if (psPVRJTable->pfnPVRSRVRemoveCmdProcList (psDevInfo->uiPVRDevID, OMAPLFB_COMMAND_COUNT) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Device %u: Couldn't unregister "
			"command processing functions\n",
			__FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID);
		return OMAPLFB_FALSE;
	}

	if (psPVRJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->uiPVRDevID) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Device %u: Couldn't remove "
			"device from PVR Services\n",
			__FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID);
		return OMAPLFB_FALSE;
	}

	OMAPLFBDeInitFBDev(psDevInfo);

	OMAPLFBSetDevInfoPtr(psDevInfo->uiFBDevID, NULL);

	OMAPLFBFreeKernelMem(psDevInfo);

	return OMAPLFB_TRUE;
}

OMAPLFB_ERROR OMAPLFBDeInit(struct drm_device *dev)
{
	unsigned uiMaxFBDevIDPlusOne = OMAPLFBMaxFBDevIDPlusOne();
	unsigned i;
	OMAPLFB_BOOL bError = OMAPLFB_FALSE;

	for(i = 0; i < uiMaxFBDevIDPlusOne; i++)
	{
		OMAPLFB_DEVINFO *psDevInfo = OMAPLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			bError |= !OMAPLFBDeInitDev(psDevInfo);
		}
	}

	return (bError) ? OMAPLFB_ERROR_INIT_FAILURE : OMAPLFB_OK;
}
