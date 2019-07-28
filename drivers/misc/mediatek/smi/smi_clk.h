/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __SMI_CLK_H__
#define __SMI_CLK_H__

#if defined(CONFIG_MACH_MT6799)
enum SMI_MASTER_ID {
	SMI_LARB0 = 0,
	SMI_LARB_MMSYS0 = SMI_LARB0,
	SMI_LARB1 = 1,
	SMI_LARB_MMSYS1 = SMI_LARB1,
	SMI_LARB2 = 2,
	SMI_LARB_IMGSYS0 = SMI_LARB2,
	SMI_LARB3 = 3,
	SMI_LARB_CAMSYS0 = SMI_LARB3,
	SMI_LARB4 = 4,
	SMI_LARB_VDECSYS = SMI_LARB4,
	SMI_LARB5 = 5,
	SMI_LARB_IMGSYS1 = SMI_LARB5,
	SMI_LARB6 = 6,
	SMI_LARB_CAMSYS1 = SMI_LARB6,
	SMI_LARB7 = 7,
	SMI_LARB_VENCSYS = SMI_LARB7,
	SMI_LARB8 = 8,
	SMI_LARB_MJCSYS = SMI_LARB8,
	SMI_COMMON = 9
};
#elif defined(CONFIG_MACH_MT6759)
enum SMI_MASTER_ID {
	SMI_LARB0 = 0,
	SMI_LARB_MMSYS0 = SMI_LARB0,
	SMI_LARB1 = 1,
	SMI_LARB_MMSYS1 = SMI_LARB1,
	SMI_LARB2 = 2,
	SMI_LARB_IMGSYS0 = SMI_LARB2,
	SMI_LARB3 = 3,
	SMI_LARB_CAMSYS0 = SMI_LARB3,
	SMI_LARB4 = 4,
	SMI_LARB_VDECSYS = SMI_LARB4,
	SMI_LARB5 = 5,
	SMI_LARB_IMGSYS1 = SMI_LARB5,
	SMI_LARB6 = 6,
	SMI_LARB_CAMSYS1 = SMI_LARB6,
	SMI_LARB7 = 7,
	SMI_LARB_VENCSYS = SMI_LARB7,
	SMI_COMMON = 8
};
#elif defined(CONFIG_MACH_MT6763)
enum SMI_MASTER_ID {
	SMI_LARB0 = 0,
	SMI_LARB_MMSYS0 = SMI_LARB0,
	SMI_LARB1 = 1,
	SMI_LARB_IMGSYS0 = SMI_LARB1,
	SMI_LARB2 = 2,
	SMI_LARB_CAMSYS0 = SMI_LARB2,
	SMI_LARB3 = 3,
	SMI_LARB_VENCSYS = SMI_LARB3,
	SMI_COMMON = 4
};
#else
enum SMI_MASTER_ID {
	SMI_LARB0 = 0,
	SMI_LARB1 = 1,
	SMI_LARB2 = 2,
	SMI_LARB3 = 3,
	SMI_LARB4 = 4,
	SMI_LARB5 = 5,
	SMI_LARB6 = 6,
	SMI_LARB7 = 7,
	SMI_LARB8 = 8,
	SMI_COMMON = 9
};
#endif
#endif
