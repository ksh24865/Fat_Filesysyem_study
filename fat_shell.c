/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : fat_shell.c                                                      */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Adaption layer between FAT File System and shell                 */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "fat_shell.h"

#define FSOPRS_TO_FATFS( a )		( FAT_FILESYSTEM* )a->pdata

typedef struct
{
	union
	{
		WORD	halfCluster[2];
		DWORD	fullCluster;
	};
	BYTE	attribute;
} PRIVATE_FAT_ENTRY;

char* my_strncpy( char* dest, const char* src, int length ) 
// FAT시스템에선 문자열의 끝을 0x20 즉, 스페이스로 구분함 따라서 
// 문자열 관리 함수 직접 만들어야 함.
{
	while( *src && *src != 0x20 && length-- > 0 )
		*dest++ = *src++;

	return dest;
}

int my_strnicmp( const char* str1, const char* str2, int length )
{
	char	c1, c2;

	while( ( ( *str1 && *str1 != 0x20 ) || ( *str2 && *str2 != 0x20 ) ) && length-- > 0 )
	{
		c1 = toupper( *str1 );
		c2 = toupper( *str2 );

		if( c1 > c2 )
			return -1;
		else if( c1 < c2 )
			return 1;

		str1++;
		str2++;
	}

	return 0;
}
// FAT_NODE의 정보를 SHELL_ENTRY에 적재 >> shell_entry의 pdata 버퍼에 fat_entry정보를 저장
int fat_entry_to_shell_entry( const FAT_NODE* fat_entry, SHELL_ENTRY* shell_entry )
											// fat_entry                root
	// FAT_NODE의 정보를 SHELL_ENTRY에 적재 >> shell_entry의 pdata 버퍼에 fat_entry정보를 저장
	// shell_entry(root)는 shell level directory entry 가짐, fat directory entry 버퍼에 보관
	// 즉, shell_entry는 언제든 양쪽 entry접근 가능 
{
	
	FAT_NODE* entry = ( FAT_NODE* )shell_entry->pdata;
	BYTE*	str;


	memset( shell_entry, 0, sizeof( SHELL_ENTRY ) ); // shell_entry초기화
	
	// shell_entry에 fat_entry정보를 변형해서 작성
	if( fat_entry->entry.attribute != ATTR_VOLUME_ID )
	{
		str = shell_entry->name;
		str = my_strncpy( str, fat_entry->entry.name, 8 ); //앞의 8자만 copy
		if( fat_entry->entry.name[8] != 0x20 ) // entry.name[8] 이 공백이 아니면
		{
			str = my_strncpy( str, ".", 1 );//.
			str = my_strncpy( str, &fat_entry->entry.name[8], 3 );//확장자
		}
	}

	if( fat_entry->entry.attribute & ATTR_DIRECTORY ||
		fat_entry->entry.attribute & ATTR_VOLUME_ID )
		shell_entry->isDirectory = 1; //해당 Entry가 디렉토리라면 디렉토리임을 저장
	else
		shell_entry->size = fat_entry->entry.fileSize; //shell_entry 사이즈 정보 저장

	*entry = *fat_entry;
	// shell_entry의 pdata 버퍼에 fat_entry정보를 저장

	return FAT_SUCCESS;
}
//fat_entry=shell_entry->pdata
int shell_entry_to_fat_entry( const SHELL_ENTRY* shell_entry, FAT_NODE* fat_entry )
{
	FAT_NODE* entry = ( FAT_NODE* )shell_entry->pdata;

	*fat_entry = *entry;

	return FAT_SUCCESS;
}

int	fs_create( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name, SHELL_ENTRY* retEntry )
{
	FAT_NODE	FATParent;
	FAT_NODE	FATEntry;
	int				result;

	shell_entry_to_fat_entry( parent, &FATParent ); // &FATParent = parent->pdata

	result = fat_create( &FATParent, name, &FATEntry );

	fat_entry_to_shell_entry( &FATEntry, retEntry ); //retEntry->pdata = &FATEntry

	return result;
}

int fs_remove( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	file;

	shell_entry_to_fat_entry( parent, &FATParent );
	fat_lookup( &FATParent, name, &file );

	return fat_remove( &file );
}

int	fs_read( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, unsigned long offset, unsigned long length, char* buffer )
{
	FAT_NODE	FATEntry;

	shell_entry_to_fat_entry( entry, &FATEntry );

	return fat_read( &FATEntry, offset, length, buffer );
}

int	fs_write( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, unsigned long offset, unsigned long length, const char* buffer )
{
	FAT_NODE	FATEntry;

	shell_entry_to_fat_entry( entry, &FATEntry );

	return fat_write( &FATEntry, offset, length, buffer );
}

static SHELL_FILE_OPERATIONS g_file =
{
	fs_create,
	fs_remove,
	fs_read,
	fs_write
};

int fs_stat( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, unsigned int* totalSectors, unsigned int* usedSectors )
{
	FAT_NODE	entry;

	return fat_df( FSOPRS_TO_FATFS( fsOprs ), totalSectors, usedSectors );
}

int adder( void* list, FAT_NODE* entry )
{
	SHELL_ENTRY_LIST*	entryList = ( SHELL_ENTRY_LIST* )list;
	SHELL_ENTRY			newEntry;

	fat_entry_to_shell_entry( entry, &newEntry );

	add_entry_list( entryList, &newEntry );

	return FAT_SUCCESS;
}

int fs_read_dir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY_LIST* list )
{
	FAT_NODE	entry;

	if( list->count )
		release_entry_list( list ); //리스트 free하는건데 처음엔 count안정해져서 넘어가

	shell_entry_to_fat_entry( parent, &entry );
	fat_read_dir( &entry, adder, list );

	return FAT_SUCCESS;
}

int is_exist( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	SHELL_ENTRY_LIST		list;
	SHELL_ENTRY_LIST_ITEM*	current;

	init_entry_list( &list );

	fs_read_dir( disk, fsOprs, parent, &list );
	current = list.first;

	while( current )				/* is directory already exist? */
	{
		if( my_strnicmp( current->entry.name, name, 12 ) == 0 )
		{
			release_entry_list( &list );
			return FAT_ERROR;		/* the directory is already exist */
		}

		current = current->next;
	}

	release_entry_list( &list );
	return FAT_SUCCESS;
}

int fs_mkdir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name, SHELL_ENTRY* retEntry )
{
	FAT_NODE		FATParent; //root ���丮
	FAT_NODE		FATEntry; //shell entry
	int					result;
	
	if( is_exist( disk, fsOprs, parent, name ) )
		return FAT_ERROR;

	shell_entry_to_fat_entry( parent, &FATParent ); // FATParent = parent->pdata

	result = fat_mkdir( &FATParent, name, &FATEntry );

	fat_entry_to_shell_entry( &FATEntry, retEntry );

	return result;
}

int fs_rmdir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	dir;

	shell_entry_to_fat_entry( parent, &FATParent );
	fat_lookup( &FATParent, name, &dir );
	// name으로 찾은 정보를 dir에 가져옴

	return fat_rmdir( &dir );
}

int fs_lookup( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	FATEntry;
	int				result;

	shell_entry_to_fat_entry( parent, &FATParent );

	result = fat_lookup( &FATParent, name, &FATEntry );

	fat_entry_to_shell_entry( &FATEntry, entry );

	return result;
}

static SHELL_FS_OPERATIONS	g_fsOprs =
{
	fs_read_dir,
	fs_stat,
	fs_mkdir,
	fs_rmdir,
	fs_lookup,
	&g_file,
	NULL
};

int fs_mount( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, SHELL_ENTRY* root )
{ // 디스크를 파일시스템의 디렉토리에 연결
	FAT_FILESYSTEM* fat;
	FAT_NODE	fat_entry;
	int		result;
	char	FATTypes[][8] = { "FAT12", "FAT16", "FAT32" };
	char	volumeLabel[12] = { 0, };

	
	*fsOprs = g_fsOprs;
	// main의 g_fs에 mount하려는 파일 시스템 operation함수를 등록해줌

	fsOprs->pdata = malloc( sizeof( FAT_FILESYSTEM ) ); 
	fat = FSOPRS_TO_FATFS( fsOprs ); //fat = fsOprs->pdata
	//메모리 할당, 파일 시스템 할당 

	ZeroMemory( fat, sizeof( FAT_FILESYSTEM ) );
	//fat파일시스템 0으로 초기화

	fat->disk = disk;
	// 할당받은 파일 시스템에 disk operation 등록
	

	result = fat_read_superblock( fat, &fat_entry ); 
	


	if( result == FAT_SUCCESS )
	{
		// 성공하면
		if( fat->FATType == 2)
			memcpy ( volumeLabel, fat->bpb.BPB32.bs.volumeLabel, 11 );
		else
			memcpy ( volumeLabel, fat->bpb.bs.volumeLabel, 11 );
		
		printf( "FAT type               : %s\n", FATTypes[fat->FATType] );
		printf( "volume label           : %s\n", volumeLabel );
		printf( "bytes per sector       : %d\n", fat->bpb.bytesPerSector );
		printf( "sectors per cluster    : %d\n", fat->bpb.sectorsPerCluster );
		printf( "number of FATs         : %d\n", fat->bpb.numberOfFATs );
		printf( "root entry count       : %d\n", fat->bpb.rootEntryCount );
		printf( "total sectors          : %u\n", ( fat->bpb.totalSectors ? fat->bpb.totalSectors : fat->bpb.totalSectors32 ) );
		printf( "\n" );
		// 결과 출력
	}

	fat_entry_to_shell_entry( &fat_entry, root ); //fat_shell.h --> root ���丮 ���� shell ���·� ����

	return result;
}

void fs_umount( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs )
{
	if( fsOprs && fsOprs->pdata )
	{
		fat_umount( FSOPRS_TO_FATFS( fsOprs ) ); //fsOprs->pdata->클러스트 리스트 초기화

		free( fsOprs->pdata ); // fsOprs->pdata 할당해제
		fsOprs->pdata = 0;
	}
}

int fs_format( DISK_OPERATIONS* disk, void* param ) 
{
	unsigned char FATType;
	char*	FATTypeString[3] = { "FAT12", "FAT16", "FAT32" };
	char*	paramStr = ( char* )param;
	int		i;

	if( param )
	{
		for( i = 0; i < 3; i++ )
		{
			if( my_strnicmp( paramStr, FATTypeString[i], 100 ) == 0 ) // 문자열 비교 통해 argv[1]로 입력받은 FAT 고르기
			{
				FATType = i;
				break;
			}
		}

		if( i == 3 ) // 입력받은 param이 FATTypeString 배열에 없으면
		{
			PRINTF( "Unknown FAT type\n" );
			return -1;
		}
	}
	else //argv[1]을 입력받지 않았을 경우
	{
		if( disk->numberOfSectors <= 8400 )  //섹터의 수로 판단하여 적절한 FATType지정
			FATType = 0;
		else if( disk->numberOfSectors <= 66600 )
			FATType = 1;
		else
			FATType = 2;
	}

	printf( "formatting as a %s\n", FATTypeString[FATType] );
	return fat_format( disk, FATType ); // 입력받은 FAT로 format
}

static SHELL_FILESYSTEM g_fat = 
{
	"FAT",
	fs_mount,
	fs_umount,
	fs_format
};

void shell_register_filesystem( SHELL_FILESYSTEM* fs )
{
	*fs = g_fat; //g_fs에 g_fat 삽입
}
