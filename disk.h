/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : disk.h                                                           */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Disk device header                                               */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#ifndef _DISK_H_
#define _DISK_H_

#include "common.h"

typedef struct DISK_OPERATIONS
{
	int		( *read_sector	)( struct DISK_OPERATIONS*, SECTOR, void* ); //한 섹터 내용 data에 복사
	int		( *write_sector	)( struct DISK_OPERATIONS*, SECTOR, const void* ); // 한 섹터에 data내용 복사
	SECTOR	numberOfSectors;
	int		bytesPerSector;
	void*	pdata;
} DISK_OPERATIONS;

#endif

