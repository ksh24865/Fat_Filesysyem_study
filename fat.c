/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : fat.c                                                            */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : FAT File System core                                             */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include "fat.h"
#include "clusterlist.h"

#define MIN( a, b )					( ( a ) < ( b ) ? ( a ) : ( b ) )
#define MAX( a, b )					( ( a ) > ( b ) ? ( a ) : ( b ) )
#define NO_MORE_CLUSER()			WARNING( "No more clusters are remained\n" );

unsigned char toupper( unsigned char ch );
int isalpha( unsigned char ch );
int isdigit( unsigned char ch );

/* calculate the 'sectors per cluster' by some conditions */
DWORD get_sector_per_clusterN( DWORD diskTable[][2], UINT64 diskSize, UINT32 bytesPerSector )
{
	int i = 0;

	do
	{
		if( ( ( UINT64 )( diskTable[i][0] * 512 ) ) >= diskSize )
			return diskTable[i][1] / ( bytesPerSector / 512 );
	}
	while( diskTable[i++][0] < 0xFFFFFFFF );

	return 0;
}

DWORD get_sector_per_cluster16( UINT64 diskSize, UINT32 bytesPerSector )
{
	DWORD	diskTableFAT16[][2] =
	{
		{ 8400,			0	},
		{ 32680,		2	},
		{ 262144,		4	},
		{ 524288,		8	},
		{ 1048576,		16	},
		/* The entries after this point are not used unless FAT16 is forced */
		{ 2097152,		32	},
		{ 4194304,		64	},
		{ 0xFFFFFFFF,	0	}
	};

	return get_sector_per_clusterN( diskTableFAT16, diskSize, bytesPerSector );
}

DWORD get_sector_per_cluster32( UINT64 diskSize, UINT32 bytesPerSector )
{
	DWORD	diskTableFAT32[][2] =
	{
		{ 66600,		0	},
		{ 532480,		1	},
		{ 16777216,		8	},
		{ 33554432,		16	},
		{ 67108864,		32	},
		{ 0xFFFFFFFF,	64	}
	};

	return get_sector_per_clusterN( diskTableFAT32, diskSize, bytesPerSector );
}

DWORD get_sector_per_cluster( BYTE FATType, UINT64 diskSize, UINT32 bytesPerSector )
{
	switch( FATType )
	{
		case 0:		/* FAT12 */
			return 1;
		case 1:		/* FAT16 */
			return get_sector_per_cluster16( diskSize, bytesPerSector );
		case 2:		/* FAT32 */
			return get_sector_per_cluster32( diskSize, bytesPerSector );
	}

	return 0;
}

/* fills the field FATSize16 and FATSize32 of the FAT_BPB */
void fill_fat_size( FAT_BPB* bpb, BYTE FATType )
{
	UINT32	diskSize = ( bpb->totalSectors32 == 0 ? bpb->totalSectors : bpb->totalSectors32 );
	UINT32	rootDirSectors = ( ( bpb->rootEntryCount * 32 ) + (bpb->bytesPerSector - 1) ) / bpb->bytesPerSector;
	UINT32	tmpVal1 = diskSize - ( bpb->reservedSectorCount + rootDirSectors );
	UINT32	tmpVal2 = ( 256 * bpb->sectorsPerCluster ) + bpb->numberOfFATs;
	UINT32	FATSize;

	if( FATType == FAT32 )
		tmpVal2 = tmpVal2 / 2;

	FATSize = ( tmpVal1 + ( tmpVal2 - 1 ) ) / tmpVal2;

	if( FATType == 32 )
	{
		bpb->FATSize16 = 0;
		bpb->BPB32.FATSize32 = FATSize;
	}
	else
		bpb->FATSize16 = ( WORD )( FATSize & 0xFFFF );
}

int fill_bpb( FAT_BPB* bpb, BYTE FATType, SECTOR numberOfSectors, UINT32 bytesPerSector )
{
	QWORD diskSize = numberOfSectors * bytesPerSector;
	FAT_BOOTSECTOR* bs;
	BYTE	filesystemType[][8] = { "FAT12   ", "FAT16   ", "FAT32   " };
	UINT32	sectorsPerCluster;

	if( FATType > 2 )
		return FAT_ERROR;

	ZeroMemory( bpb, sizeof( FAT_BPB ) );

	
	bpb->jmpBoot[0] = 0xEB;
	bpb->jmpBoot[1] = 0x00;		
	bpb->jmpBoot[2] = 0x90;	
	//Jump Boot Code : Boot Code로 점프하기 위한 코드
	memcpy( bpb->OEMName, "MSWIN4.1", 8 );
	// oemname설정

	sectorsPerCluster			= get_sector_per_cluster( FATType, diskSize, bytesPerSector );

	if( sectorsPerCluster == 0 )
	{
		WARNING( "The number of sector is out of range\n" );
		return -1;
	}
	

	bpb->bytesPerSector			= bytesPerSector;
	bpb->sectorsPerCluster		= sectorsPerCluster;
	bpb->reservedSectorCount	= ( FATType == FAT32 ? 32 : 1 );
	bpb->numberOfFATs			= 1;
	bpb->rootEntryCount			= ( FATType == FAT32 ? 0 : 512 );
	bpb->totalSectors			= ( numberOfSectors < 0x10000 ? ( UINT16 ) numberOfSectors : 0 );
	
	bpb->media					= 0xF8;
	fill_fat_size( bpb, FATType );
	bpb->sectorsPerTrack		= 0;
	bpb->numberOfHeads			= 0;
	bpb->totalSectors32			= ( numberOfSectors >= 0x10000 ? numberOfSectors : 0 );
	//bpb 구조체 원소들 채워줌

	if( FATType == FAT32 )
	{
		bpb->BPB32.extFlags		= 0x0081;	/* active FAT : 1, only one FAT is active */
		bpb->BPB32.FSVersion	= 0;
//		bpb->BPB32.rootCluster	= 2;
		bpb->BPB32.FSInfo		= 1;
		bpb->BPB32.backupBootSectors	= 6;
		bpb->BPB32.backupBootSectors	= 0;
		ZeroMemory( bpb->BPB32.reserved, 12 );
	}
	//FAT32의 경우 추가설정

	if( FATType == FAT32 )
		bs = &bpb->BPB32.bs;
	else
		bs = &bpb->bs;
	
	if( FATType == FAT12 )
		bs->driveNumber	= 0x00;
	else
		bs->driveNumber	= 0x80;
	// drive number set

	bs->reserved1		= 0; //예약된 영역
	bs->bootSignature	= 0x29; // 확장부트서명
	bs->volumeID		= 0; // 볼륨시리얼번호
	memcpy( bs->volumeLabel, VOLUME_LABEL, 11 ); // 볼륨 레이블 set
	memcpy( bs->filesystemType, filesystemType[FATType], 8 ); // file system type 설정

	return FAT_SUCCESS;
}

int get_fat_type( FAT_BPB* bpb )
{
	UINT32	totalSectors, dataSector, rootSector, countOfClusters, FATSize;

	rootSector = ( ( bpb->rootEntryCount * 32 ) + ( bpb->bytesPerSector - 1 ) ) / bpb->bytesPerSector;

	if( bpb->FATSize16 != 0 )
		FATSize = bpb->FATSize16;
	else
		FATSize = bpb->BPB32.FATSize32;

	if( bpb->totalSectors != 0 )
		totalSectors = bpb->totalSectors;
	else
		totalSectors = bpb->totalSectors32;

	dataSector = totalSectors - ( bpb->reservedSectorCount + ( bpb->numberOfFATs * FATSize ) + rootSector );
	countOfClusters = dataSector / bpb->sectorsPerCluster;

	if( countOfClusters < 4085 )
		return FAT12;
	else if( countOfClusters < 65525 )
		return FAT16;
	else
		return FAT32;

	return FAT_ERROR;
}

FAT_ENTRY_LOCATION get_entry_location( const FAT_DIR_ENTRY* entry )
{
	FAT_ENTRY_LOCATION	location;

	location.cluster	= GET_FIRST_CLUSTER( *entry );
	location.sector		= 0;
	location.number		= 0;

	return location;
}

/* fills the reserved fields of FAT */
int fill_reserved_fat( FAT_BPB* bpb, BYTE* sector )
{
	BYTE	FATType;
	DWORD*	shutErrBit12;
	WORD*	shutBit16;
	WORD*	errBit16;
	DWORD*	shutBit32;
	DWORD*	errBit32;

	FATType = get_fat_type( bpb );
	if( FATType == FAT12 )
	{
		shutErrBit12 = ( DWORD* )sector;

		*shutErrBit12 = 0xFF0 << 20;
		*shutErrBit12 |= ( ( DWORD )bpb->media & 0x0F ) << 20;
		*shutErrBit12 |= MS_EOC12 << 8;
	}
	else if( FATType == FAT16 )
	{
		shutBit16 = ( WORD* )sector;
		errBit16 = ( WORD* )sector + sizeof( WORD );

		*shutBit16 = 0xFFF0 | bpb->media;
		*errBit16 = MS_EOC16;
	}
	else
	{
		shutBit32 = ( DWORD* )sector;
		errBit32 = ( DWORD* )sector + sizeof( DWORD );

		*shutBit32 = 0x0FFFFFF0 | bpb->media;
		*errBit32 = MS_EOC32;
	}

	return FAT_SUCCESS;
}

int clear_fat( DISK_OPERATIONS* disk, FAT_BPB* bpb )
{
	/*
	FAT영역 초기화 코드
	*/
	UINT32	i, end;
	UINT32	FATSize;
	SECTOR	fatSector;
	BYTE	sector[MAX_SECTOR_SIZE];

	ZeroMemory( sector, sizeof( sector ) ); // sector배열 0으로 초기화
	fatSector = bpb->reservedSectorCount;

	if( bpb->FATSize16 != 0 )
		FATSize = bpb->FATSize16;
	else
		FATSize = bpb->BPB32.FATSize32; 
	// FATSize 지정

	end = fatSector + ( FATSize * bpb->numberOfFATs );
	//bpb의 가ㅄ을 읽어서

	fill_reserved_fat( bpb, sector );
	// fat의 예약된 영역(cluster 0,1) sector에 채움
	disk->write_sector( disk, fatSector, sector );
	// Fat table을 위해 사용될 공간은 모두 0으로 초기화되어 사용가능 상태를 나타내야 함 
	// but, cluster 0,1에 대응하는 fat링크의 경우 fat버전에 따라 특별한 가ㅄ을 가져야 함.
	// 해당 내용 disk에 write하고
	

	ZeroMemory( sector, sizeof( sector ) );
	// sector 다시 0으로 초기화

	for( i = fatSector + 1; i < end; i++ )
		disk->write_sector( disk, i, sector );
	// 위에서 write한 영역을 제외한 나머지 FAT영역을 free상태로 초기화

	return FAT_SUCCESS;
}

int create_root( DISK_OPERATIONS* disk, FAT_BPB* bpb )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	SECTOR	rootSector = 0;
	FAT_DIR_ENTRY*	entry;

	ZeroMemory( sector, MAX_SECTOR_SIZE ); //sector 배열 0으로 Set
	entry = ( FAT_DIR_ENTRY* )sector; 

	
	memcpy( entry->name, VOLUME_LABEL, 11 );
	entry->attribute = ATTR_VOLUME_ID;
	//entry에 여러 정보(name<볼륨레이블>, attribute<기능>) 삽입

	/* Mark as no more directory is in here */
	entry++;
	entry->name[0] = DIR_ENTRY_NO_MORE;  // 현재 맨뒤entry의 다음번째 즉, 마지막 entry임을 표시

	if( get_fat_type( bpb ) == FAT32 )
	{
		/* Not implemented yet */
	}
	else
		rootSector = bpb->reservedSectorCount + ( bpb->numberOfFATs * bpb->FATSize16 ); 
		//reserved 영역과 FAT영역 까지의 sector를 구해서 그 다음부터인 사용가능 sector를 구해서 루트 디렉토리에게 줌

	disk->write_sector( disk, rootSector, sector ); // root섹터에 entry 정보 Write

	return FAT_SUCCESS;
}

int get_fat_sector( FAT_FILESYSTEM* fs, SECTOR cluster, SECTOR* fatSector, DWORD* fatEntryOffset )
{
	DWORD	fatOffset;

	switch( fs->FATType )
	{//파일 시스템에 맞게 fatOffset을 설정
	case FAT32:
		fatOffset = cluster * 4; 
		break;
	case FAT16:
		fatOffset = cluster * 2;
		break;
	case FAT12:
		fatOffset = cluster + ( cluster / 2 );
		break;
	default:
		WARNING( "Illegal file system type\n" );
		fatOffset = 0;
		break;
	}

	*fatSector		= fs->bpb.reservedSectorCount + ( fatOffset / fs->bpb.bytesPerSector );//클러스터 FAT Entry가 몇 번째 FAT 섹터에 위치하는지
	*fatEntryOffset	= fatOffset % fs->bpb.bytesPerSector; // 그 섹터내에서 몇번째 Entry인지

	return FAT_SUCCESS;
}

int prepare_fat_sector( FAT_FILESYSTEM* fs, SECTOR cluster, SECTOR* fatSector, DWORD* fatEntryOffset, BYTE* sector )
{
	get_fat_sector( fs, cluster, fatSector, fatEntryOffset ); 
	// 몇번째 섹터의 몇번째 entry인지
	fs->disk->read_sector( fs->disk, *fatSector, sector ); //disk의 해당 *fatSector를 read해서 sector버퍼에 저장

	if( fs->FATType == FAT12 && *fatEntryOffset == fs->bpb.bytesPerSector - 1 ) //?
	{
		// fatEntryOffset이 bytesPerSector즉 sector의 마지막 지점이면 다음 섹터를 sector버퍼로 read
		fs->disk->read_sector( fs->disk, *fatSector + 1, &sector[fs->bpb.bytesPerSector] );
		return 1;
	}

	return 0;
}

/* Read a FAT entry from FAT Table */
// FATable에서 cluster 번호 위치에 적힌 번호를 리턴 <다음 클러스터 불러옴>
DWORD get_fat( FAT_FILESYSTEM* fs, SECTOR cluster )
{
	BYTE	sector[MAX_SECTOR_SIZE * 2]; 
	SECTOR	fatSector; 
	DWORD	fatEntryOffset; 
	
	
	prepare_fat_sector( fs, cluster, &fatSector, &fatEntryOffset, sector );
	// fatSector = FATable[cluster]의 디스크에서의 위치 중 sector인덱스
	// sector = 해당 sector데이터
	// fatEntryOffset = FATable[cluster]의 디스크 에서의 위치 중 sector offset

	switch( fs->FATType )
	{
		// FAT 버전에 따라서 FAT table의 크기가 다르기 때문에 하나의 entry를 추출해서 return하는 방식이 다름
		// 결국 하는 짓은 해당 섹터[오프셋]위치의 파일의 주소를 리턴
	case FAT32:
		return ( *( ( DWORD* )&sector[fatEntryOffset] ) ) & 0xFFFFFFF;
	case FAT16:
		return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) );
	case FAT12:
		/*
		12bit를 추출
		even일 경우, 12bits * 2n = 24bits * n이고 바이트 단위 정렬이 됨
		odd일 경우, 12bits * 2(n+1) -> 바이트 정렬이 안됨
		-> n이 1일 경우, 12bit에 접근하기 위해 8bit 즉 바이트로 접근한 뒤에 4bit를 버린다.
		*/
		if( cluster & 1 )	/* Cluster number is ODD	*/
			return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) >> 4 );
		else				/* Cluster number is EVEN	*/
			return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) & 0xFFF );
	}

	return FAT_ERROR;
}

/* Write a FAT entry to FAT Table */
int set_fat( FAT_FILESYSTEM* fs, SECTOR cluster, DWORD value )
{
	BYTE	sector[MAX_SECTOR_SIZE * 2];
	SECTOR	fatSector; //몇번째섹터인지
	DWORD	fatEntryOffset; // 해당섹터의 몇번째인지
	int		result;

	result = prepare_fat_sector( fs, cluster, &fatSector, &fatEntryOffset, sector );
	// 몇번째 Sector의 몇번째 byteoffset인지 계산해서
	switch( fs->FATType )
	{
		//그 장소에 value(eoc)를 파일시스템에 맞게 비트연산 해서 삽입
	case FAT32:
		value &= 0x0FFFFFFF;
		*( ( DWORD* )&sector[fatEntryOffset] ) &= 0xF0000000;
		*( ( DWORD* )&sector[fatEntryOffset] ) |= value;
		break;
	case FAT16:
		*( ( WORD* )&sector[fatEntryOffset] ) = ( WORD )value;
		break;
	case FAT12:
		if( cluster & 1 )
		{
			value <<= 4;
			*( ( WORD* )&sector[fatEntryOffset] ) &= 0x000F;
		}
		else
		{
			value &= 0x0FFF;
			*( ( WORD* )&sector[fatEntryOffset] ) &= 0xF000;
		}
		*( ( WORD* )&sector[fatEntryOffset] ) |= ( WORD )value;
		break;
	}

	fs->disk->write_sector( fs->disk, fatSector, sector );
	//sector[fatEntryOffset]를 설정해서 fatSector에 write
	if( result ) //prepare에서 sector offset이 sector 넘어가면 , 다음 섹터에 write
		fs->disk->write_sector( fs->disk, fatSector + 1, &sector[fs->bpb.bytesPerSector] );

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Format disk as a specified file system                                     */
/******************************************************************************/
int fat_format( DISK_OPERATIONS* disk, BYTE FATType )
{
	FAT_BPB bpb;  // BIOS Parameter Block 생성, 파일 시스템이 어떻게 구성되어 있는지 알려주기 위한 주요한 파라미터 블록

	if( fill_bpb( &bpb, FATType, disk->numberOfSectors, disk->bytesPerSector ) != FAT_SUCCESS ) // bpb에 FAT타입, 섹터개수, 섹터 사이즈 정보 채움
		return FAT_ERROR;
	
	disk->write_sector( disk, 0, &bpb ); //disk의 0번 섹터에 bpb정보 작성

	PRINTF( "bytes per sector       : %u\n", bpb.bytesPerSector );//512
	PRINTF( "sectors per cluster    : %u\n", bpb.sectorsPerCluster ); //1
	PRINTF( "number of FATs         : %u\n", bpb.numberOfFATs ); //1
	PRINTF( "root entry count       : %u\n", bpb.rootEntryCount ); //512
	PRINTF( "total sectors          : %u\n", ( bpb.totalSectors ? bpb.totalSectors : bpb.totalSectors32 ) ); //4096
	PRINTF( "\n" );
	//작성 정보 출력

	clear_fat( disk, &bpb ); // FAT영역 초기화 코드
	create_root( disk, &bpb ); // root sector 생성및 정보삽입

	return FAT_SUCCESS;
}
// JUMP Boot Code 검사
int validate_bpb( FAT_BPB* bpb )
{
	int FATType;

	if( !( bpb->jmpBoot[0] == 0xEB && bpb->jmpBoot[2] == 0x90 ) &&
		!( bpb->jmpBoot[0] == 0xE9 ) ) 
		return FAT_ERROR;

	FATType = get_fat_type( bpb );

	if( FATType < 0 )
		return FAT_ERROR;

	return FAT_SUCCESS;
}


int read_root_sector( FAT_FILESYSTEM* fs, SECTOR sectorNumber, BYTE* sector )
{
	SECTOR	rootSector;

	rootSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->bpb.FATSize16 );
	//root디렉토리의 위치<FAT의 다음영역>를 가져옴
	
	return fs->disk->read_sector( fs->disk, rootSector + sectorNumber, sector ); //해당 sector를 read
}


int write_root_sector( FAT_FILESYSTEM* fs, SECTOR sectorNumber, const BYTE* sector )
{
	SECTOR	rootSector;

	rootSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->bpb.FATSize16 );
	//root디렉토리의 위치<FAT의 다음영역>를 가져옴

	return fs->disk->write_sector( fs->disk, rootSector + sectorNumber, sector );//해당 sector를 write
}

/* Translate logical cluster and sector numbers to a physical sector number */
SECTOR	calc_physical_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber )
{
	SECTOR	firstDataSector;
	SECTOR	firstSectorOfCluster;
	SECTOR	rootDirSectors;

	rootDirSectors = ( ( fs->bpb.rootEntryCount * 32 ) + ( fs->bpb.bytesPerSector - 1 ) ) / fs->bpb.bytesPerSector ;
	firstDataSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->FATSize ) + rootDirSectors;
	firstSectorOfCluster = ( ( clusterNumber - 2 ) * fs->bpb.sectorsPerCluster ) + firstDataSector;

	return firstSectorOfCluster + sectorNumber;
}

int read_data_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber, BYTE* sector )
{
	return fs->disk->read_sector( fs->disk, calc_physical_sector( fs, clusterNumber, sectorNumber ), sector );
}

int write_data_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber, const BYTE* sector )
{
	return fs->disk->write_sector( fs->disk, calc_physical_sector( fs, clusterNumber, sectorNumber ), sector );
}

/* search free clusters from FAT and add to free cluster list */
int search_free_clusters( FAT_FILESYSTEM* fs )
{
	UINT32	totalSectors, dataSector, rootSector, countOfClusters, FATSize;
	UINT32	i, cluster;

	// root 디렉토리의 크기를 sector 단위로 계산

	rootSector = ( ( fs->bpb.rootEntryCount * 32 ) + ( fs->bpb.bytesPerSector - 1 ) ) / fs->bpb.bytesPerSector;

	if( fs->bpb.FATSize16 != 0 ) //fat16이면
		FATSize = fs->bpb.FATSize16;
	else
		FATSize = fs->bpb.BPB32.FATSize32; //fat32면

	if( fs->bpb.totalSectors != 0 )
		totalSectors = fs->bpb.totalSectors;
	else
		totalSectors = fs->bpb.totalSectors32;

	// data 영역의 크기를 sector 단위로 계산
	// reserved 영역, FAT 영역, root 디렉토리를 할당하고 남은 영역
	dataSector = totalSectors - ( fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * FATSize ) + rootSector );

	// 섹터영역 클러스터 크기로 나누어 cluster 단위로 환산
	countOfClusters = dataSector / fs->bpb.sectorsPerCluster;


	// 0, 1번 cluster는 다른 목적으로 사용
	// 실제 data가 들어가는 cluster는 2번부터 시작
	for( i = 2; i < countOfClusters; i++ )
	{
		cluster = get_fat( fs, i );
		if( cluster == FREE_CLUSTER )
			add_free_cluster( fs, i ); // fs->clustlist에 freeclust 추가
	}

	return FAT_SUCCESS;
}

int fat_read_superblock( FAT_FILESYSTEM* fs, FAT_NODE* root )
{
	/* 
	1. 부트 파라미터 블록을 읽어들여 내용이 유효한지 검사
	2. 클러스터 리스트를 초기화하고 FAT table로 부터 free list를 구성

	fs가 가리키는 파일시스템은 모두 0으로 초기화 되어있음
	root가 가리키는 곳은 초기화 되지 않음
	// fs는 main에서 선언된 g_fsOprs의 void* pdata필드에 연결된 FAT_FILESYSTEM 구조체를 가리킴
	*/
	INT		result;
	BYTE	sector[MAX_SECTOR_SIZE];
 
	if( fs == NULL || fs->disk == NULL )
	{
		WARNING( "DISK_OPERATIONS : %p\nFAT_FILESYSTEM : %p\n", fs, fs->disk );
		return FAT_ERROR;
	}

	
	if( fs->disk->read_sector( fs->disk, 0, &fs->bpb ) )
		return FAT_ERROR;
	//fs->disk로 bpb를 읽어들임
	//0번 sector는 bpb의 영역

	result = validate_bpb( &fs->bpb );// bpb의 유효성을 bootjmp code와 FATType으로 검사
	if( result )
	{
		WARNING( "BPB validation is failed\n" );
		return FAT_ERROR;
	}

	fs->FATType = get_fat_type( &fs->bpb ); // bpb의 타입 얻어옴 
	if( fs->FATType > FAT32 ) //FAT12~32 아니면
		return FAT_ERROR;

	
	if( read_root_sector( fs, 0, sector ) )
		return FAT_ERROR;
	//sector 버퍼에 Root directory sector읽어옴


	ZeroMemory( root, sizeof( FAT_NODE ) );
	memcpy( &root->entry, sector, sizeof( FAT_DIR_ENTRY ) );
	//sector에 들어있는 root directory sector 데이터를 (인자로 받은)root로 copy
	root->fs = fs;
	
	

	fs->EOCMark = get_fat( fs, 1 );
	if( fs->FATType == 2 ) //32
	{
		if( fs->EOCMark & SHUT_BIT_MASK32 )
			WARNING( "disk drive did not dismount correctly\n" );
		if( fs->EOCMark & ERR_BIT_MASK32 )
			WARNING( "disk drive has error\n" );
	}
	else
	{
		if( fs->FATType == 1) //16
		{
			if( fs->EOCMark & SHUT_BIT_MASK16 )
				PRINTF( "disk drive did not dismounted\n" );
			if( fs->EOCMark & ERR_BIT_MASK16 )
				PRINTF( "disk drive has error\n" );
		}
	}
	// FAT 파일 시스템에 맞는 EOC(end of cluster)인지 체크, 버전마다 eoc비트열이 모두 다름
	
	if( fs->bpb.FATSize16 != 0 ) // FATSize16이 0이면 FAT32
		fs->FATSize = fs->bpb.FATSize16;
	else
		fs->FATSize = fs->bpb.BPB32.FATSize32;
	// FATsize를 FAT32인 경우 FAT32size, FAT(12,16)인 경우 FATSize16으로 설정

	init_cluster_list( &fs->freeClusterList );
	// freeClusterList초기화


	search_free_clusters( fs );
	// free cluster 검색 및 클러스트 리스트에 추가

	memset( root->entry.name, 0x20, 11 );
	// entry.name 초기화
	return FAT_SUCCESS;
}

/******************************************************************************/
/* On unmount file system                                                     */
/******************************************************************************/
void fat_umount( FAT_FILESYSTEM* fs )
{
	release_cluster_list( &fs->freeClusterList );
}

int read_dir_from_sector( FAT_FILESYSTEM* fs, FAT_ENTRY_LOCATION* location, BYTE* sector, FAT_NODE_ADD adder, void* list )
{
	UINT		i, entriesPerSector;
	FAT_DIR_ENTRY*	dir;
	FAT_NODE	node;

	entriesPerSector = fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );
	dir = ( FAT_DIR_ENTRY* )sector;

	for( i = 0; i < entriesPerSector; i++ )
	{
		if( dir->name[0] == DIR_ENTRY_FREE )
			;
		else if( dir->name[0] == DIR_ENTRY_NO_MORE )
			break;
		else if( !( dir->attribute & ATTR_VOLUME_ID ) )
		{
			node.fs = fs;
			node.location = *location;
			node.location.number = i;
			node.entry = *dir;
			adder( list, &node );		/* call the callback function that adds entries to list */
		}

		dir++;
	}

	return ( i == entriesPerSector ? 0 : -1 );
}

DWORD get_MS_EOC( BYTE FATType )
{
	switch( FATType )
	{
	case FAT12:
		return MS_EOC12;
	case FAT16:
		return MS_EOC16;
	case FAT32:
		return MS_EOC32;
	}

	WARNING( "Incorrect FATType(%u)\n", FATType );
	return -1;
}

int is_EOC( BYTE FATType, SECTOR clusterNumber )
{
	switch( FATType )
	{
	case FAT12:
		if( EOC12 <= ( clusterNumber & 0xFFF ) )
			return -1;

		break;
	case FAT16:
		if( EOC16 <= ( clusterNumber & 0xFFFF ) )
			return -1;

		break;
	case FAT32:
		if( EOC32 <= ( clusterNumber & 0x0FFFFFFF ) )
			return -1;
		break;
	default:
		WARNING( "Incorrect FATType(%u)\n", FATType );
	}

	return 0;
}

/******************************************************************************/
/* Read all entries in the current directory                                  */
/******************************************************************************/
int fat_read_dir( FAT_NODE* dir, FAT_NODE_ADD adder, void* list )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	SECTOR	i, j, rootEntryCount;
	FAT_ENTRY_LOCATION location;

	if( IS_POINT_ROOT_ENTRY( dir->entry ) && ( dir->fs->FATType == FAT12 || dir->fs->FATType == FAT16 ) )
	{ //root 디렉토리인지
		if( dir->fs->FATType != FAT32 )
			rootEntryCount = dir->fs->bpb.rootEntryCount;

		for( i = 0; i < rootEntryCount; i++ )
		{
			read_root_sector( dir->fs, i, sector );
			location.cluster = 0; //root라서
			location.sector = i;
			location.number = 0;
			if( read_dir_from_sector( dir->fs, &location, sector, adder, list ) )
				break;
		}
	}
	else
	{ //root 아니라면
		i = GET_FIRST_CLUSTER( dir->entry );
		do
		{
			for( j = 0; j < dir->fs->bpb.sectorsPerCluster; j++ )
			{
				read_data_sector( dir->fs, i, j, sector );
				location.cluster = i;
				location.sector = j;
				location.number = 0;

				if( read_dir_from_sector( dir->fs, &location, sector, adder, list ) )
					break;
			}
			i = get_fat( dir->fs, i );
		} while( !is_EOC( dir->fs->FATType, i ) && i != 0 );
	}

	return FAT_SUCCESS;
}

int add_free_cluster( FAT_FILESYSTEM* fs, SECTOR cluster )
{
	return push_cluster( &fs->freeClusterList, cluster );
}

SECTOR alloc_free_cluster( FAT_FILESYSTEM* fs )
{
	SECTOR	cluster;

	if( pop_cluster( &fs->freeClusterList, &cluster ) == FAT_ERROR )
		return 0;

	return cluster;
}

SECTOR span_cluster_chain( FAT_FILESYSTEM* fs, SECTOR clusterNumber )
{
	UINT32	nextCluster;

	nextCluster = alloc_free_cluster( fs );

	if( nextCluster )
	{
		set_fat( fs, clusterNumber, nextCluster ); // 현재 cluster에 새로운 cluster 번호 set
		set_fat( fs, nextCluster, get_MS_EOC( fs->FATType ) ); // 새로운 cluster에 EOC(End Of Cluster?) set

	}

	return nextCluster;
}

int find_entry_at_sector( const BYTE* sector, const BYTE* formattedName, UINT32 begin, UINT32 last, UINT32* number )
{
	// begin에서 last까지 formattedName을 가진 entry를 sector에서 검색해서 그 인덱스를 number에 저장
	UINT32	i;
	const FAT_DIR_ENTRY*	entry = ( FAT_DIR_ENTRY* )sector;

	for( i = begin; i <= last; i++ )
	{
		if( formattedName == NULL )
		{// formattedName == NULL인 경우

			if( entry[i].name[0] != DIR_ENTRY_FREE && entry[i].name[0] != DIR_ENTRY_NO_MORE )
			{
				*number = i;
				return FAT_SUCCESS;
				// 현재 사용중인 첫번째 entry읽어옴
			}
		}
		else 
		{ // formattedName을 가진 경우
			if( ( formattedName[0] == DIR_ENTRY_FREE || formattedName[0] == DIR_ENTRY_NO_MORE ) &&
				( formattedName[0] == entry[i].name[0] ) )
			{ 
				// 새로운 dir_entry 추가할 때 추가될 dir_entry의 위치 찾음
				// entry[i]와 formattedName을 비교해서 동일하면 
				*number = i;
				return FAT_SUCCESS;
			}

			if( memcmp( entry[i].name, formattedName, MAX_ENTRY_NAME_LENGTH ) == 0 )
			{
				// entry name을 검색할 때 해당 dir_entry를 찾은 경우
				// entry[i]와 formattedName을 비교해서 동일하면 
				*number = i;
				return FAT_SUCCESS;
			}
		}

		if( entry[i].name[0] == DIR_ENTRY_NO_MORE )
		{
			// dir_entry의 끝 -> 검색 중지, 해당 위치 number에 저장
			// null 로 검색할 경우 && dir_entry 배열이 비어있는 경우
			*number = i;
			return -2;
		}
	}

	*number = i;
	return -1;
}

int find_entry_on_root( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* formattedName, FAT_NODE* ret )
{
	// sector버퍼
	BYTE	sector[MAX_SECTOR_SIZE];
	UINT32	i, number;
	UINT32	lastSector;
	UINT32	entriesPerSector, lastEntry;
	INT32	begin = first->number;
	INT32	result;
	FAT_DIR_ENTRY*	entry;

	entriesPerSector	= fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );
	lastEntry			= entriesPerSector - 1;
	lastSector			= fs->bpb.rootEntryCount / entriesPerSector;

	// root sector 영역에서 sector단위로 모든 Sector 검색
	for( i = first->sector; i <= lastSector; i++ )
	{
		read_root_sector( fs, i, sector );
		// root sector중에서 i 번째 sector를 sector 버퍼에 write

		entry = ( FAT_DIR_ENTRY* )sector;
		// 읽어온 sector의 첫 FAT_DIR_ENTRY를 entry에 연결

		result = find_entry_at_sector( sector, formattedName, begin, lastEntry, &number );
		begin = 0;
		/*
		하나의 sector에서 찾고자 하는 formattedName을 가진 entry를 검사해서 해당 sector를 찾고, 
		sector에서의 entry의 인덱스를 number에 넣어준다.
		*/

		if( result == -1 ) //탐색실패
			continue;
		else
		{
			if( result == -2 ) // 찾은 Directory entry가 없을 경우
				return FAT_ERROR;
			else // 찾은 경우
			{
				memcpy( &ret->entry, &entry[number], sizeof( FAT_DIR_ENTRY ) );
				// formattedName으로 검색하여 찾은 FAT_DIR_ENTRY를 ret->entry에 write

				ret->location.cluster	= 0;
				// cluster위치 고정

				ret->location.sector	= i;
				// sector의 실제 위치

				ret->location.number	= number;
				// sector내부에서의 실제 인덱스

				ret->fs = fs;
				// 파일 시스템 연결
			}

			return FAT_SUCCESS;
		}
	}

	return FAT_ERROR;
}

int find_entry_on_data( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* formattedName, FAT_NODE* ret )
{
	// sector 버퍼
	BYTE	sector[MAX_SECTOR_SIZE];
	UINT32	i, number;
	UINT32	entriesPerSector, lastEntry;
	UINT32	currentCluster;
	INT32	begin = first->number;
	INT32	result;
	FAT_DIR_ENTRY*	entry;

	currentCluster		= first->cluster;
	entriesPerSector	= fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );
	lastEntry			= entriesPerSector - 1;

	while( -1 )
	{
		UINT32	nextCluster;

		for( i = first->sector; i < fs->bpb.sectorsPerCluster; i++ )
		{ // first->sector는 directory entry의 parent directory entry가 존재하는 sector
		  // currentCluster에 존재하는 모든 sector를 검사

			read_data_sector( fs, currentCluster, i, sector );
			entry = ( FAT_DIR_ENTRY* )sector;
			// currentCluster로 cluster에 접근하고 i로 sector에 접근해서 sector버퍼에 저장

			result = find_entry_at_sector( sector, formattedName, begin, lastEntry, &number );
			begin = 0;
			// sector내부 dir_entry 위치(location->number) 검색

			if( result == -1 ) //못찾음, 다음sector
				continue;
			else
			{
				if( result == -2 ) //엔트리 끝
					return FAT_ERROR;
				else //찾
				{
					memcpy( &ret->entry, &entry[number], sizeof( FAT_DIR_ENTRY ) );
					
					ret->location.cluster	= currentCluster;
					ret->location.sector	= i;
					ret->location.number	= number;
			
					ret->fs = fs;
					//찾은 정보 ret에 복사
				}

				return FAT_SUCCESS;
			}
		}

		nextCluster = get_fat( fs, currentCluster );
		// 다음 sector 검색 위해 currentcluster에 대응하는 FATable entry를 얻어옴

		if( is_EOC( fs->FATType, nextCluster ) )
		// nextCluster가 EOC인 경우
			break;
		else if( nextCluster == 0)
			break;
		/*
		nextCluster가 정상적으로 cluster chain에서 다음 부분을 가리키고 있는지 검사<0이면 안됨>
		*/


		currentCluster = nextCluster;
		// 다음 클러스터로 이동
	}
	// dir_entry의 끝까지 순회했지만 찾지 못함, 에러
	return FAT_ERROR;
}

/* entryName = NULL -> Find any valid entry */
///entryName을 가지는 entry가 parent디렉토리에 있는지 확인한다.
int lookup_entry( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* entryName, FAT_NODE* ret )
{
	/*
	찾고자 하는 entryName이 존재하는지 탐색
	*/
	if( first->cluster == 0 && ( fs->FATType == FAT12 || fs->FATType == FAT16 ) )
	// root sector 생성시 cluster정보 모두 0으로 초기화 so, first->cluster == 0이면 현재위치 == root
		return find_entry_on_root( fs, first, entryName, ret ); //root에서 entry찾기
	else
		return find_entry_on_data( fs, first, entryName, ret ); //그 외 데이터에서 entry찾기
}

int set_entry( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* location, const FAT_DIR_ENTRY* value )
{
	// data영역(cluster 단위로 관리) location 위치에 dir_entry 저장
	// 실제 data영역(disk)에 구조체 정보를 저장하는 단계
	BYTE	sector[MAX_SECTOR_SIZE];
	FAT_DIR_ENTRY*	entry;

	
	if( location->cluster == 0 && ( fs->FATType == FAT12 || fs->FATType == FAT16 ) )
	{ //location이 root디렉토리 영역일 경우
		/* 
		디스크에는 오직 sector 단위로만 읽고 쓰기가 가능하기 때문에 쓰고자 하는 sector를 전부 버퍼에 읽고, 
		특정 영역을 버퍼에서 변경한 뒤, 다시 디스크의 sector에 써주는 식으로 write를 해야 한다.
		읽어오는 과정 없이는 바꾸고자 하는 부분을 제외한 나머지 부분의 데이터를 모르기 때문에 write를 하면 변경한 부분을 제외한 나머지 부분이 원하지 않게 변경된다.
		*/
		read_root_sector( fs, location->sector, sector );
		//sector에 루트 디렉토리 위치 가져옴

		entry = ( FAT_DIR_ENTRY* )sector;
		entry[location->number] = *value;
		//location->number위치에 value 추가
		write_root_sector( fs, location->sector, sector );
		// 
	}
	else
	{ //location이 root디렉토리 영역이 아닐 경우
		read_data_sector( fs, location->cluster, location->sector, sector );
		// read_root_sector가 아닌 read_data_sector를 사용하여 0이 아닌 cluster에 접근

		entry = ( FAT_DIR_ENTRY* )sector;
		entry[location->number] = *value;

		write_data_sector( fs, location->cluster, location->sector, sector );
	}

	return FAT_ERROR;
}

int insert_entry( const FAT_NODE* parent, FAT_NODE* newEntry, BYTE overwrite )
{
	//부모 디렉토리 아래에 새로운 dir_entry추가
	FAT_ENTRY_LOCATION	begin; // 새로운 dir
	FAT_NODE			entryNoMore;
	BYTE				entryName[2] = { 0, };

	begin.cluster = GET_FIRST_CLUSTER( parent->entry );
	begin.sector = 0;
	begin.number = 0;
	// parent directory의 시작 cluster를 get

	if( !( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) ) && overwrite )
	{ /*
	  parent가 root가 아니고 overwrite인 경우, parent 디렉토리의 처음 부분에 newEntry를 insert하고, 그 다음 entry에서 end of entries를 설정
	  */
		begin.number = 0;
		
		set_entry( parent->fs, &begin, &newEntry->entry );
		// begin entry에 &newEntry->entry을 set

		newEntry->location = begin;
		// newEntry 위치 설정

		/* End of entries */
		begin.number = 1;
		// 위에서 set한 entry의 다음 entry를 가리킴

		ZeroMemory( &entryNoMore, sizeof( FAT_NODE ) );

		entryNoMore.entry.name[0] = DIR_ENTRY_NO_MORE;
		// End of entry를 설정

		set_entry( parent->fs, &begin, &entryNoMore.entry );
		// 다음 영역에 &entryNoMore.entry을 set
		

		return FAT_SUCCESS;
	}

	/* find empty(unused) entry, overwrite가 아닌 경우, parent 디렉토리에서 빈 entry를 찾아야 한다. */
	entryName[0] = DIR_ENTRY_FREE;
	if( lookup_entry( parent->fs, &begin, entryName, &entryNoMore ) == FAT_SUCCESS )
	{ // 빈 entry찾은 경우
		set_entry( parent->fs, &entryNoMore.location, &newEntry->entry );
		//entryNoMore.location에 newEntry->entry를 set
		newEntry->location = entryNoMore.location;
	}
	else 
	{ // 못 찾은 경우
		if( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) )
		{
			// root entry이면 
			// dir_entry 개수를 구한 후 root 디렉토리의 최대치보다 크다면 에러
			UINT32	rootEntryCount = newEntry->location.sector * ( parent->fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY ) ) + newEntry->location.number;
			if( rootEntryCount >= parent->fs->bpb.rootEntryCount )
			{
				WARNING( "Cannot insert entry into the root entry\n" );
				return FAT_ERROR;
			}
		}

		/* 
		add new entry to end 
		free 상태의 entry를 찾지 못한 경우, 가장 마지막, 즉 DIR_ENTRY_NO_MORE을 나타내던 디렉토리 엔트리에 insert되어야함.
		이때 DIR_ENTRY_NO_MORE 디렉토리 엔트리를 추가해야함.
		*/
		entryName[0] = DIR_ENTRY_NO_MORE;
		if( lookup_entry( parent->fs, &begin, entryName, &entryNoMore ) == FAT_ERROR ) //DIR_ENTRY_NO_MORE를 찾지 못한 경우
			return FAT_ERROR;

		// 찾은 경우

		set_entry( parent->fs, &entryNoMore.location, &newEntry->entry );
		newEntry->location = entryNoMore.location;
		// entryNoMore.location에서 정보 가져옴
		
		entryNoMore.location.number++;
		// DIR_ENTRY_NO_MORE를 추가하기 위함

		if( entryNoMore.location.number == ( parent->fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY ) ) )
		{ // 추가하려는 number가 sector에 저장될 수 있는 디렉토리 엔트리의 최대 개수를 넘었으면

			entryNoMore.location.sector++;
			entryNoMore.location.number = 0;
			// 그 다음 sector에 저장되도록 한다.

			if( entryNoMore.location.sector == parent->fs->bpb.sectorsPerCluster )
			{ // sector가 주어진 cluster에 저장될 수 있는 최대 sector를 넘었으면
				if( !( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) ) )
				{ // root디렉토리가 아닐 경우

					entryNoMore.location.cluster = span_cluster_chain( parent->fs, entryNoMore.location.cluster );
					// cluster chain을 확장하여 그 다음 cluster인덱스를 받고

					if( entryNoMore.location.cluster == 0 )
					{ // 확장 실패시 에러 출력
						NO_MORE_CLUSER();
						return FAT_ERROR;
					}

					entryNoMore.location.sector = 0; // 확장 성공시 첫 sector로 설정
				}
			}
		}

		/* End of entries를 정해진 위치에 저장 */
		set_entry( parent->fs, &entryNoMore.location, &entryNoMore.entry );
	}

	return FAT_SUCCESS;
}

void upper_string( char* str, int length )
{
	while( *str && length-- > 0 )
	{
		*str = toupper( *str );
		str++;
	}
}

int format_name( FAT_FILESYSTEM* fs, char* name )
{
	// name을 파일 시스템 형식에 맞게 고침

	UINT32	i, length;
	UINT32	extender = 0, nameLength = 0;
	UINT32	extenderCurrent = 8;
	BYTE	regularName[MAX_ENTRY_NAME_LENGTH];

	memset( regularName, 0x20, sizeof( regularName ) );
	length = strlen( name );
	
	// hidden directory일 경우
	if( strncmp( name, "..", 2 ) == 0 ) // 이름이 ..인 경우
	{
		memcpy( name, "..         ", 11 ); //11글자 맞춰줌 뒷부분 스페이스로 채움
		return FAT_SUCCESS;
	}
	else if( strncmp( name, ".", 1 ) == 0 )	
	{
		memcpy( name, ".          ", 11 );
		return FAT_SUCCESS;
	}
	// hidden 아닐경우
	if( fs->FATType == FAT32 )
	{
	}
	else
	{
		upper_string( name, MAX_ENTRY_NAME_LENGTH );
		//name을 대문자로 변경

		for( i = 0; i < length; i++ )
		{
			if( name[i] != '.' && !isdigit( name[i] ) && !isalpha( name[i] ) )
				return FAT_ERROR;
			// name에 '.',숫자,알파벳 제외한 문자 있으면 에러

			if( name[i] == '.' )
			{
				if( extender )
					return FAT_ERROR;		/* dot character is allowed only once */
				extender = 1;
			}
			// .은 두개이상일 수 없음

			else if( isdigit( name[i] ) || isalpha( name[i] ) )
			{
				// 파일명과 확장자 구분
				if( extender )
					regularName[extenderCurrent++] = name[i]; // .이후
				else
					regularName[nameLength++] = name[i]; // .이전
			}

			else
				return FAT_ERROR;			/* non-ascii name is not allowed */
		}

		if( nameLength > 8 || nameLength == 0 || extenderCurrent > 11 )
			return FAT_ERROR;
	}

	memcpy( name, regularName, sizeof( regularName ) );
	return FAT_SUCCESS;
}

/******************************************************************************/
/* Create new directory                                                       */
/******************************************************************************/
int fat_mkdir( const FAT_NODE* parent, const char* entryName, FAT_NODE* ret )
{
	FAT_NODE		dotNode, dotdotNode;
	DWORD			firstCluster; 
	BYTE			name[MAX_NAME_LENGTH];
	int				result;

	strncpy( name, entryName, MAX_NAME_LENGTH );

	if( format_name( parent->fs, name ) ) // 입력받은 name을 파일시스템에 맞게 검사 및 변형
		return FAT_ERROR;

	/* newEntry */
	ZeroMemory( ret, sizeof( FAT_NODE ) );
	memcpy( ret->entry.name, name, MAX_ENTRY_NAME_LENGTH ); // 이름 설정
	ret->entry.attribute = ATTR_DIRECTORY; // 용도를 디렉토리로 설정
	firstCluster = alloc_free_cluster( parent->fs ); //freecluster 할당
	// newEntry<ret>에 entryName,attribute을 등록, firstcluster가져오기

	if( firstCluster == 0 )
	{
		NO_MORE_CLUSER();
		return FAT_ERROR;
	}
	
	set_fat( parent->fs, firstCluster, get_MS_EOC( parent->fs->FATType ) ); 
	// FATable에 해당 클러스터 FATentry를 EOC로 바꿈         //get_MS_EOC : FAT시스템에 맞는 EOC호출
	SET_FIRST_CLUSTER( ret->entry, firstCluster ); // ret->entry의 firstClusterLO에 firstcluster변수<할당 받은 클러스터>를 등록
	result = insert_entry( parent, ret, 0 ); // parent아래에 ret삽입
	if( result )
		return FAT_ERROR;

	ret->fs = parent->fs;

	/* dotEntry 현재위치 설정*/
	ZeroMemory( &dotNode, sizeof( FAT_NODE ) );
	memset( dotNode.entry.name, 0x20, 11 );
	dotNode.entry.name[0] = '.';
	dotNode.entry.attribute = ATTR_DIRECTORY;
	SET_FIRST_CLUSTER( dotNode.entry, firstCluster ); 
	// dotNode.entry<현재위치>의 irstClusterLO에 firstcluster변수<할당 받은 클러스터>를 등록
	insert_entry( ret, &dotNode, DIR_ENTRY_OVERWRITE ); //ret아래에 . 삽입

	/* dotdotEntry */
	ZeroMemory( &dotdotNode, sizeof( FAT_NODE ) );
	memset( dotdotNode.entry.name, 0x20, 11 );
	dotdotNode.entry.name[0] = '.';
	dotdotNode.entry.name[1] = '.';
	dotdotNode.entry.attribute = ATTR_DIRECTORY;
	SET_FIRST_CLUSTER( dotdotNode.entry, GET_FIRST_CLUSTER( parent->entry ) );
	// dotdotNode.entry<상위폴더위치>의 irstClusterLO에 firstcluster변수<할당 받은 클러스터>를 등록
	insert_entry( ret, &dotdotNode, 0 ); // ret아래에 .. 삽입

	return FAT_SUCCESS;
}
// 클러스터체인 따라가면서 eoc나올때까지 cluster 지워주고, freeclusterlist에 추가
int free_cluster_chain( FAT_FILESYSTEM* fs, DWORD firstCluster )
{
	DWORD	currentCluster = firstCluster;
	DWORD	nextCluster;

	while( !is_EOC( fs->FATType, currentCluster ) && currentCluster != FREE_CLUSTER )
	{
		// 클러스터체인 따라가면서 eoc나올때까지 cluster 지워주고, freeclusterlist에 추가
		nextCluster = get_fat( fs, currentCluster );
		set_fat( fs, currentCluster, FREE_CLUSTER );
		add_free_cluster( fs, currentCluster );
		currentCluster = nextCluster;
	}

	return FAT_SUCCESS;
}

int has_sub_entries( FAT_FILESYSTEM* fs, const FAT_DIR_ENTRY* entry )
{
	FAT_ENTRY_LOCATION	begin;
	FAT_NODE			subEntry;

	begin = get_entry_location( entry );
	begin.number = 2;		/* Ignore the '.' and '..' entries */

	if( !lookup_entry( fs, &begin, NULL, &subEntry ) )
		return FAT_ERROR;

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Remove directory                                                           */
/******************************************************************************/
int fat_rmdir( FAT_NODE* dir )
{
	if( has_sub_entries( dir->fs, &dir->entry ) ) // sub_entry 가졌다면
		return FAT_ERROR;

	if( !( dir->entry.attribute & ATTR_DIRECTORY ) )		/* Is directory? */
		return FAT_ERROR;

	dir->entry.name[0] = DIR_ENTRY_FREE; // 이름 초기화
	set_entry( dir->fs, &dir->location, &dir->entry ); //초기화한거 disk에 set
	free_cluster_chain( dir->fs, GET_FIRST_CLUSTER( dir->entry ) ); // cluster에서 초기화

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Lookup entry(file or directory)                                            */
/******************************************************************************/
int fat_lookup( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry )
{
	FAT_ENTRY_LOCATION	begin;
	BYTE	formattedName[MAX_NAME_LENGTH] = { 0, };

	begin.cluster = GET_FIRST_CLUSTER( parent->entry );
	begin.sector = 0;
	begin.number = 0;

	strncpy( formattedName, entryName, MAX_NAME_LENGTH );

	if( format_name( parent->fs, formattedName ) ) //name을 파일 시스템 형식에 맞게 고침, 대문자화, 파일명 확장자 분리
		return FAT_ERROR;

	if( IS_POINT_ROOT_ENTRY( parent->entry ) )
		begin.cluster = 0;

	return lookup_entry( parent->fs, &begin, formattedName, retEntry );
}

/******************************************************************************/
/* Create new file                                                            */
/******************************************************************************/
int fat_create( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry )
{
	FAT_ENTRY_LOCATION	first;
	BYTE				name[MAX_NAME_LENGTH] = { 0, };
	int					result;

	strncpy( name, entryName, MAX_NAME_LENGTH ); //name에 엔트리네임 복사

	if( format_name( parent->fs, name ) ) // 입력받은 name을 파일시스템에 맞게 검사 및 변형
		return FAT_ERROR;

	// newEntry에 entryname을 등록
	ZeroMemory( retEntry, sizeof( FAT_NODE ) );
	memcpy( retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH );


	first.cluster = parent->entry.firstClusterLO;
	first.sector = 0;
	first.number = 0;

	if( lookup_entry( parent->fs, &first, name, retEntry ) == FAT_SUCCESS )
		return FAT_ERROR;
	// entryName을 가지는 file이 parent디렉토리에 있는지 확인한다.

	retEntry->fs = parent->fs;
	result = insert_entry( parent, retEntry, 0 ); // entryName을 가진 file이 parent디렉토리에 존재하지 않을 경우 파일 생성
	
	if( result )
		return FAT_ERROR;
	
	return FAT_SUCCESS;
}

/******************************************************************************/
/* Read file                                                                  */
/******************************************************************************/
int fat_read( FAT_NODE* file, unsigned long offset, unsigned long length, char* buffer )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	DWORD	currentOffset, currentCluster, clusterSeq = 0;
	DWORD	clusterNumber, sectorNumber, sectorOffset;
	DWORD	readEnd;
	DWORD	clusterSize, clusterOffset = 0;

	currentCluster = GET_FIRST_CLUSTER( file->entry ); // 읽을 file->entry의 first cluster
	readEnd = MIN( offset + length, file->entry.fileSize ); // 어디까지 읽을건지
	
	currentOffset = offset; //읽기 시작할 offset

	clusterSize = ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster ); //클러스터 사이즈
	clusterOffset = clusterSize; // cluster 끝까지 읽었는지 확인 위한 offset

	while( offset > clusterOffset )
	{ // offset이 clusteroffset보다 클 수 없음(?)
	// 만약 크면 다음 클러스터 가져옴
		currentCluster = get_fat( file->fs, currentCluster );
		clusterOffset += clusterSize;
		clusterSeq++;
	}

	while( currentOffset < readEnd )
	{ // currentOffset으로 readEnd 까지 읽어냄
		DWORD	copyLength;

		clusterNumber	= currentOffset / ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );
		// offset / cluster한개 사이즈로 넘버링
		if( clusterSeq != clusterNumber ) 
		{
			// 처음엔 둘다 0, 읽으면서 currentOffset이 증가하고 결국 clusterNumber++됨 따라서 다음 cluster읽어야 할 차례
			clusterSeq++; // 클러스터 SEQ 증가 시키고
			currentCluster = get_fat( file->fs, currentCluster ); //다음 클러스터 가져옴
		}
		sectorNumber	= ( currentOffset / ( file->fs->bpb.bytesPerSector ) ) % file->fs->bpb.sectorsPerCluster;
		// 클러스터 내 sector num
		sectorOffset	= currentOffset % file->fs->bpb.bytesPerSector;
		// sector 내 byte offset
		if( read_data_sector( file->fs, currentCluster, sectorNumber, sector ) ) //한 섹터 내용 data에 복사
			break; // disk 입출력 오류난 경우(-1리턴함)

		copyLength = MIN( file->fs->bpb.bytesPerSector - sectorOffset, readEnd - currentOffset );
		//한 섹터씩 읽으므로 sectoroffset은 항상0이므로 결국 한 섹터 크기임 
		//copyLength = min(한섹터크기, readend까지 남은 byte) 
		//즉 평소엔 한 섹터 크기, 마지막 섹터에서만 남은 offset나타냄

		memcpy( buffer,
				&sector[sectorOffset],
				copyLength );

		buffer += copyLength; // 다음섹터로 or 끝으로
		currentOffset += copyLength;// 다음섹터로 or 끝으로
	}

	return currentOffset - offset;
}

/******************************************************************************/
/* Write file                                                                 */
/******************************************************************************/
int fat_write( FAT_NODE* file, unsigned long offset, unsigned long length, const char* buffer )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	DWORD	currentOffset, currentCluster, clusterSeq = 0;
	DWORD	clusterNumber, sectorNumber, sectorOffset;
	DWORD	readEnd;
	DWORD	clusterSize;

	currentCluster = GET_FIRST_CLUSTER( file->entry ); 
	readEnd = offset + length; // 쓰기 동작은 파일 크기 고려 X, cluster 추가해 가면서 쓰기 진행
	//offset<0> +length<입력받은 size>를 쓰기동작의 한계점으로 지정

	currentOffset = offset;

	clusterSize = ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster ); // 클러스터 크기 = 섹터 크기*클러스터당 섹터개수
	while( offset > clusterSize )
	{
		currentCluster = get_fat( file->fs, currentCluster ); //Read a FAT entry from FAT Table
		clusterSize += clusterSize;
		clusterSeq++;
	}

	while( currentOffset < readEnd )
	{
		DWORD	copyLength;

		clusterNumber	= currentOffset / ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );
		//현재 offset을 클러스터 크기로 나눠 번호 매김
		if( currentCluster == 0 ) // cluster를 할당해주지 않은 비어있는 파일일 때
		{
			currentCluster = alloc_free_cluster( file->fs ); // cluster 할당
			if( currentCluster == 0 )
			{
				NO_MORE_CLUSER();
				return FAT_ERROR;
			}

			SET_FIRST_CLUSTER( file->entry, currentCluster ); //currentCluster를 file->entry의 first_cluster로 지정
			set_fat( file->fs, currentCluster, get_MS_EOC( file->fs->FATType ) ); //생성 파일의 fat_entry FATable에 작성
		}

		if( clusterSeq != clusterNumber ) // 다음 cluster에 써야 한다면
		{
			DWORD nextCluster;
			clusterSeq++;

			nextCluster = get_fat( file->fs, currentCluster );
			if( is_EOC( file->fs->FATType, nextCluster ) ) // nextCluster가 eoc이면
			{
				nextCluster = span_cluster_chain( file->fs, currentCluster ); // 클러스터 할당

				if( nextCluster == 0 )
				{
					NO_MORE_CLUSER();
					break;
				}
			}
			currentCluster = nextCluster;
		}
		
		sectorNumber	= ( currentOffset / ( file->fs->bpb.bytesPerSector ) ) % file->fs->bpb.sectorsPerCluster;
		// cluster 에서의 sector offset

		sectorOffset	= currentOffset % file->fs->bpb.bytesPerSector;
		// sector 에서의 byte offset 
		// sectorOffset는 마지막을 제외하고는 항상 0임 왜냐하면 currentOffset이 copyLength(한 섹터 크기)만큼 증가하기 때문
		// 마지막엔 남은 offset 수 이므로 0이 아님

		copyLength = MIN( file->fs->bpb.bytesPerSector - sectorOffset, readEnd - currentOffset );
		// 보통 한 섹터씩 작성하므로 작성할 크기가 한 섹터 넘는 경우 file->fs->bpb.bytesPerSector - sectorOffset는 bytesPerSector와 동일
		// 남은 작성 크기가 한 섹터보다 작은 경우 >> 작성해야할 length - 작성한 offset >> 남은 작성 수만 작성하면 됨. >> 섹터 더이상 안 읽어와도 됨 

		if( copyLength != file->fs->bpb.bytesPerSector )
		{
			//마지막에 copyLength와 한 섹터 크기가 달라짐 
			if( read_data_sector( file->fs, currentCluster, sectorNumber, sector ) )
				break;
		}

		memcpy( &sector[sectorOffset],
				buffer,
				copyLength ); // 한 섹터만큼 메모리 세팅

		if( write_data_sector( file->fs, currentCluster, sectorNumber, sector ) )
			break;

		buffer += copyLength; // 한 섹터만큼 버퍼 증가
		currentOffset += copyLength; // 한 섹터만큼 offset증가
	}

	file->entry.fileSize = MAX( currentOffset, file->entry.fileSize ); // file size set
	set_entry( file->fs, &file->location, &file->entry ); // 실제 DATA영역에 해당 ENTRY 저장

	return currentOffset - offset;
}

/******************************************************************************/
/* Remove file                                                                */
/******************************************************************************/
int fat_remove( FAT_NODE* file )
{
	if( file->entry.attribute & ATTR_DIRECTORY )		/* 디렉토리면 에러*/
		return FAT_ERROR;

	file->entry.name[0] = DIR_ENTRY_FREE;
	set_entry( file->fs, &file->location, &file->entry );
	free_cluster_chain( file->fs, GET_FIRST_CLUSTER( file->entry ) );

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Disk free spaces                                                           */
/******************************************************************************/
//bpb 읽어서 특성 출력
int fat_df( FAT_FILESYSTEM* fs, UINT32* totalSectors, UINT32* usedSectors )
{
	if( fs->bpb.totalSectors != 0 )
		*totalSectors = fs->bpb.totalSectors;
	else
		*totalSectors = fs->bpb.totalSectors32;

	*usedSectors = *totalSectors - ( fs->freeClusterList.count * fs->bpb.sectorsPerCluster );

	return FAT_SUCCESS;
}

