/**********************************************************************
 *
 * Copyright (C) Imagination Technologies Ltd. All rights reserved.
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

#if !defined(__PVR_DRM_H__)
#define __PVR_DRM_H__

#include "pvr_drm_shared.h"

#if defined(SUPPORT_DRI_DRM)
#define	PVR_DRM_MAKENAME_HELPER(x, y) x ## y
#define	PVR_DRM_MAKENAME(x, y) PVR_DRM_MAKENAME_HELPER(x, y)

#if defined(PVR_DRI_DRM_PLATFORM_DEV)
#define	LDM_DEV	struct platform_device
#endif

int PVRCore_Init(void);
void PVRCore_Cleanup(void);
int PVRSRVOpen(struct drm_device *dev, struct drm_file *pFile);
void PVRSRVRelease(void *pvPrivData);

#if defined(SUPPORT_DRI_DRM_EXTERNAL) || defined(PVR_DRI_DRM_PLATFORM_DEV)
int PVRSRVDriverProbe(LDM_DEV *pDevice);
int PVRSRVDriverRemove(LDM_DEV *pDevice);
int PVRSRVDriverSuspend(LDM_DEV *pDevice, pm_message_t state);
int PVRSRVDriverResume(LDM_DEV *pDevice);
void PVRSRVDriverShutdown(LDM_DEV *pDevice);
#else
int PVRSRVDriverSuspend(struct drm_device *pDevice, pm_message_t state);
int PVRSRVDriverResume(struct drm_device *pDevice);
#endif

int PVRSRV_BridgeDispatchKM(struct drm_device *dev, void *arg, struct drm_file *pFile);

#if defined(SUPPORT_DRI_DRM_EXT)
#define	DRI_DRM_STATIC
int PVRSRVDrmLoad(struct drm_device *dev, unsigned long flags);
int PVRSRVDrmUnload(struct drm_device *dev);
int PVRSRVDrmOpen(struct drm_device *dev, struct drm_file *file);
#if defined(PVR_LINUX_USING_WORKQUEUES)
DRI_DRM_STATIC int PVRSRVDrmRelease(struct inode *inode, struct file *filp);
#else
void PVRSRVDrmPostClose(struct drm_device *dev, struct drm_file *file);
#endif
int PVRDRMIsMaster(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile);
int PVRDRMUnprivCmd(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile);
int PVRDRM_Dummy_ioctl(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile);
#else
#define	DRI_DRM_STATIC	static
#endif	

#if defined(DISPLAY_CONTROLLER)
extern int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device *);
extern void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device *);
extern int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Suspend)(struct drm_device *);
extern int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Resume)(struct drm_device *);
#if defined(PVR_DISPLAY_CONTROLLER_DRM_IOCTL)
extern int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Ioctl)(struct drm_device *dev, void *arg, struct drm_file *pFile);
#endif
#endif

#if defined(PDUMP)
int dbgdrv_init(void);
void dbgdrv_cleanup(void);
IMG_INT dbgdrv_ioctl(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile);
#endif

#if !defined(SUPPORT_DRI_DRM_EXT)
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
#define        PVR_DRM_SRVKM_IOCTL     _IO(0, DRM_PVR_SRVKM)
#define        PVR_DRM_IS_MASTER_IOCTL _IO(0, DRM_PVR_IS_MASTER)
#define        PVR_DRM_UNPRIV_IOCTL    _IO(0, DRM_PVR_UNPRIV)
#define        PVR_DRM_DBGDRV_IOCTL    _IO(0, DRM_PVR_DBGDRV)
#define        PVR_DRM_DISP_IOCTL      _IO(0, DRM_PVR_DISP)
#else
#define        DRM_IOCTL_PVR_SRVKM     DRM_IO(DRM_COMMAND_BASE + DRM_PVR_SRVKM)
#define        DRM_IOCTL_PVR_DISP      DRM_IO(DRM_COMMAND_BASE + DRM_PVR_DISP)
#define        DRM_IOCTL_PVR_IS_MASTER DRM_IO(DRM_COMMAND_BASE + DRM_PVR_IS_MASTER)
#define        DRM_IOCTL_PVR_UNPRIV    DRM_IO(DRM_COMMAND_BASE + DRM_PVR_UNPRIV)
#define        DRM_IOCTL_PVR_DBGDRV    DRM_IO(DRM_COMMAND_BASE + DRM_PVR_DBGDRV)
#endif
#endif	

#endif	

#endif 


