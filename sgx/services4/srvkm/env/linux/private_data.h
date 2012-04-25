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

#ifndef __INCLUDED_PRIVATE_DATA_H_
#define __INCLUDED_PRIVATE_DATA_H_

#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
#include <linux/list.h>
#include <drm/drmP.h>
#endif

typedef struct
{
	
	IMG_UINT32 ui32OpenPID;

	
#if defined (SUPPORT_SID_INTERFACE)
	IMG_SID hKernelMemInfo;
#else
	IMG_HANDLE hKernelMemInfo;
#endif

#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	
	struct list_head sDRMAuthListItem;

	struct drm_file *psDRMFile;
#endif

#if defined(SUPPORT_MEMINFO_IDS)
	
	IMG_UINT64 ui64Stamp;
#endif 

	
	IMG_HANDLE hBlockAlloc;

#if defined(SUPPORT_DRI_DRM_EXT)
	IMG_PVOID pPriv;	
#endif
}
PVRSRV_FILE_PRIVATE_DATA;

#if defined(SUPPORT_DRI_DRM_EXTERNAL)
#include <linux/omap_drv.h>
extern int pvr_mapper_id;
static inline PVRSRV_FILE_PRIVATE_DATA * get_private(struct drm_file *file)
{
	return omap_drm_file_priv(file, pvr_mapper_id);
}
static inline void set_private(struct drm_file *file, PVRSRV_FILE_PRIVATE_DATA *priv)
{
	omap_drm_file_set_priv(file, pvr_mapper_id, priv);
}
#elif defined(SUPPORT_DRI_DRM)
static inline PVRSRV_FILE_PRIVATE_DATA * get_private(struct drm_file *file)
{
	return file->driver_priv;
}
static inline void set_private(struct drm_file *file, PVRSRV_FILE_PRIVATE_DATA *priv)
{
	file->driver_priv = priv;
}
#else
static inline PVRSRV_FILE_PRIVATE_DATA * get_private(struct file *file)
{
	return file->private_data;
}
static inline void set_private(struct file *file, PVRSRV_FILE_PRIVATE_DATA *priv)
{
	file->private_data = priv;
}
#endif


#endif 

