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

#if !defined(__PVR_DRM_SHARED_H__)
#define __PVR_DRM_SHARED_H__

#if defined(SUPPORT_DRI_DRM)

#if defined(SUPPORT_DRI_DRM_EXT)
#define        DRM_PVR_SRVKM           DRM_PVR_RESERVED1
#define        DRM_PVR_DISP            DRM_PVR_RESERVED2
#define        DRM_PVR_BC              DRM_PVR_RESERVED3
#define        DRM_PVR_IS_MASTER       DRM_PVR_RESERVED4
#define        DRM_PVR_UNPRIV          DRM_PVR_RESERVED5
#define        DRM_PVR_DBGDRV          DRM_PVR_RESERVED6
#else	
#define        DRM_PVR_SRVKM           0x00
#define        DRM_PVR_DISP            0x01
#define        DRM_PVR_BC              0x02
#define        DRM_PVR_IS_MASTER       0x03
#define        DRM_PVR_UNPRIV          0x04
#define        DRM_PVR_DBGDRV          0x05
#endif	

#define	PVR_DRM_UNPRIV_INIT_SUCCESFUL	0 

#endif

#endif 


