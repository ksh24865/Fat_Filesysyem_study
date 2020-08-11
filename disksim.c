/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : disksim.c                                                        */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Disk simulator                                                   */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include <stdlib.h>
#include <memory.h>
#include "fat.h"
#include "disk.h"
#include "disksim.h"

typedef struct
{
	char*	address;
} DISK_MEMORY;

int disksim_read( DISK_OPERATIONS* this, SECTOR sector, void* data );
int disksim_write( DISK_OPERATIONS* this, SECTOR sector, const void* data );

int disksim_init( SECTOR numberOfSectors, unsigned int bytesPerSector, DISK_OPERATIONS* disk ) 
{	// pdata에 main에서 요청한 disk 크기 만큼 할당해서 연결

	if( disk == NULL ) 
		return -1;

	disk->pdata = malloc( sizeof( DISK_MEMORY ) ); //disk.pdata에 메모리 할당
	if( disk->pdata == NULL )// 할당 실패시
	{
		disksim_uninit( disk );
		return -1;
	}

	( ( DISK_MEMORY* )disk->pdata )->address = ( char* )malloc( bytesPerSector * numberOfSectors ); //주소영역 메모리 할당
	if( disk->pdata == NULL )
	{
		disksim_uninit( disk );
		return -1;
	}

	disk->read_sector	= disksim_read;
	disk->write_sector	= disksim_write;
	disk->numberOfSectors	= numberOfSectors;
	disk->bytesPerSector	= bytesPerSector;
	//메인에서의 DISK_OPERATIONS 즉, g_disk에 함수 및 디스크 크기 등록
	return 0;
}

void disksim_uninit( DISK_OPERATIONS* this ) // pdata메모리 할당 해제
{
	if( this )
	{
		if( this->pdata )
			free( this->pdata ); 
	}
}

int disksim_read( DISK_OPERATIONS* this, SECTOR sector, void* data )
{
	char* disk = ( ( DISK_MEMORY* )this->pdata )->address; 

	if( sector < 0 || sector >= this->numberOfSectors )
		return -1;

	memcpy( data, &disk[sector * this->bytesPerSector], this->bytesPerSector ); //한 섹터 내용 data에 복사

	return 0;
}

int disksim_write( DISK_OPERATIONS* this, SECTOR sector, const void* data )
{
	char* disk = ( ( DISK_MEMORY* )this->pdata )->address;

	if( sector < 0 || sector >= this->numberOfSectors )
		return -1;

	memcpy( &disk[sector * this->bytesPerSector], data, this->bytesPerSector ); // 한 섹터에 data내용 복사
	return 0;
}

