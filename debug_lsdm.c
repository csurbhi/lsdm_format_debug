#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <linux/types.h>
#include "format_metadata.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

//#include <zlib.h>

#define ZONE_SZ (256 * 1024 * 1024)
#define BLK_SZ 4096
#define NR_BLKS_PER_ZONE ZONE_SZ /BLK_SZ
#define NR_CKPT_COPIES 2
#define NR_BLKS_SB 2
#define NR_SECTORS_IN_BLK 8
#define BITS_IN_BYTE 8

#define NR_EXT_ENTRIES_PER_SEC_for_Debug 6

/* Our LBA is associated with a block
 * rather than a sector
 */

/* For YUXIN:
 * Go to all the functions that call write_to_disk(); these functions
 * need to be converted to read them from the disk and then printing
 * them out in a formatted output.
 */

unsigned int crc32(int d, unsigned char *buf, unsigned int size)
{
	return 0;
}

int open_disk(char *dname)
{
	int fd;

	fd = open(dname, O_RDWR);
	if (fd < 0) {
		perror("Could not open the disk: ");
		exit(errno);
	}
	return fd;
}

int read_from_disk(int fd, char *buf, int len, int sectornr)
{
	int ret = 0;
	unsigned long long offset = sectornr * 512;


	ret = lseek(fd, offset, SEEK_SET);
	if (ret < 0) {
		perror("Error in lseek: \n");
		exit(errno);
	}
	ret = read(fd, buf, len);
	if (ret < 0) {
		perror("Error while writing: ");
		exit(errno);
	}
	return (ret);
}



__le32 get_zone_count()
{
	__le32 zone_count = 0;
	/* Use zone queries and find this eventually
	 * Doing this manually for now for a 20GB
	 * harddisk and 256MB zone size.
	 */
	zone_count = 80;
	return zone_count;
}

/* Note,  that this also account for the first few metadata zones.
 * but we do not care right now. While writing the extents, its
 * just for the data area. So we end up allocating slightly
 * more space than is strictly necessary.
 */
__le64 get_nr_blks(struct stl_sb *sb)
{
	unsigned long nr_blks = (sb->zone_count << (sb->log_zone_size - sb->log_block_size));
	return nr_blks;
}

/* Stores as many entries as there are 4096 blks. 2 zones is 131072
 * entries. That requires 586 blocks and 1 block of bitmap.
 * 1 block of bitmap can accomodate details for 4096 blocks.
 * Hence we say that as many entries as can be accomodated in 4096
 * blocks. One block has 48 entries.
 */

__le32 get_revmap_blk_count(struct stl_sb *sb)
{
	unsigned nr_rm_entries = 2 << (sb->log_zone_size - sb->log_block_size);
	unsigned nr_rm_entries_per_blk = NR_EXT_ENTRIES_PER_SEC * NR_SECTORS_IN_BLK;
	unsigned nr_rm_blks = nr_rm_entries / nr_rm_entries_per_blk;


	printf("\n nr_rm_entries: %d", nr_rm_entries);
	if (nr_rm_entries % nr_rm_entries_per_blk)
		nr_rm_blks++;
	return nr_rm_blks;
}

/* We store a tm entry for every 4096 block
 *
 * This accounts for the blks in the main area and excludes the blocks
 * for the metadata. Hence we do NOT call get_nr_blks()
 */
__le32 get_tm_blk_count(struct stl_sb *sb)
{
	u64 nr_tm_entries = (sb->zone_count_main << (sb->log_zone_size - sb->log_block_size));
	u32 nr_tm_entries_per_blk = BLOCK_SIZE / sizeof(struct tm_entry);
	u64 nr_tm_blks = nr_tm_entries / nr_tm_entries_per_blk;
	if (nr_tm_entries % nr_tm_entries_per_blk)
		nr_tm_blks++;
	return nr_tm_blks;
}


__le32 get_revmap_bm_blk_count(struct stl_sb *sb)
{
	unsigned nr_revmap_blks = sb->blk_count_revmap;
	unsigned nr_bytes = nr_revmap_blks / BITS_IN_BYTE;
	if (nr_revmap_blks % BITS_IN_BYTE)
		nr_bytes++;
	unsigned nr_blks = nr_bytes / BLOCK_SIZE;
	if (nr_bytes % BLOCK_SIZE)
		nr_blks++;
	printf("\n nr_revmap_blks: %d", nr_revmap_blks);
	printf("\n revmap_bm_blk_count: %d", nr_blks);
	return nr_blks;

}


__le32 get_nr_zones(struct stl_sb *sb)
{
	return sb->zone_count;
}



__le32 get_sit_blk_count(struct stl_sb *sb)
{
	unsigned int one_sit_sz = 80; /* in bytes */
	unsigned int nr_sits_in_blk = BLK_SZ / one_sit_sz;
	__le32 nr_sits = get_nr_zones(sb);
	unsigned int blks_for_sit = nr_sits / nr_sits_in_blk;
	if (nr_sits % nr_sits_in_blk > 0)
		blks_for_sit = blks_for_sit + 1;

	return blks_for_sit;
}

/* We write all the metadata blks sequentially in the CMR zones
 * that can be rewritten in place
 */
__le32 get_metadata_zone_count(struct stl_sb *sb)
{
	/* we add the 2 for the superblock */
	unsigned int metadata_blk_count = NR_BLKS_SB + sb->blk_count_revmap + sb->blk_count_ckpt + sb->blk_count_revmap_bm + sb->blk_count_tm + sb->blk_count_sit;
	unsigned int nr_blks_in_zone = (1 << sb->log_zone_size)/(1 << sb->log_block_size);
	unsigned int metadata_zone_count = metadata_blk_count / nr_blks_in_zone;
	if (metadata_blk_count % nr_blks_in_zone > 0)
		metadata_zone_count  = metadata_zone_count + 1;
	return metadata_zone_count;
}

/* we consider the main area to have the
 * reserved zones
 */
__le32 get_main_zone_count(struct stl_sb *sb)
{
	__le32 main_zone_count = 0;
	/* we are not subtracting reserved zones from here */
	main_zone_count = sb->zone_count - get_metadata_zone_count(sb);
	return main_zone_count;
}

#define RESERVED_ZONES	10
/* TODO: use zone querying to find this out */
__le32 get_reserved_zone_count()
{
	__le32 reserved_zone_count = 0;
	/* We will use the remaining reserved zone counts
	 * for this
	 */
	reserved_zone_count = RESERVED_ZONES;
	return reserved_zone_count;
}


/* We write one raw ckpt in one block
 * If the first LBA is 0, then the first
 * zone has (zone_size/blk_size) - 1 LBAs
 * the first block in the second zone is
 * the CP and that just takes the next
 * LBA as ZONE_SZ/BLK_SZ
 * The blk adderss is 256MB;
 */
__le64 get_revmap_pba()
{
	return (NR_BLKS_SB * NR_SECTORS_IN_BLK);
}

__le64 get_tm_pba(struct stl_sb *sb)
{
	u64 tm_pba = sb->revmap_pba + ( sb->blk_count_revmap * NR_SECTORS_IN_BLK);
	return tm_pba;
}

__le64 get_revmap_bm_pba(struct stl_sb *sb)
{
	u64 revmap_bm_pba = sb->tm_pba + (sb->blk_count_tm * NR_SECTORS_IN_BLK);
	return revmap_bm_pba;
}

__le64 get_ckpt1_pba(struct stl_sb *sb)
{
	u64 cp1_pba  = sb->revmap_bm_pba + (sb->blk_count_revmap_bm * NR_SECTORS_IN_BLK);
	return cp1_pba;
}


__le64 get_sit_pba(struct stl_sb *sb)
{
	return sb->ckpt2_pba + NR_SECTORS_IN_BLK;
}

__le64 get_zone0_pba(struct stl_sb *sb)
{
	return sb->sit_pba + (sb->blk_count_sit * NR_SECTORS_IN_BLK);
}

void read_sb(int fd, unsigned long sectornr)
{
	struct stl_sb *sb;
	int ret = 0;
	unsigned long long offset = sectornr * 512;

	sb = (struct stl_sb *)malloc(BLK_SZ);
	if (!sb)
		exit(-1);
	memset(sb, 0, BLK_SZ);

	printf("\n *********************\n");

	ret = lseek(fd, offset, SEEK_SET);
	if (ret < 0) {
		perror("\n Could not lseek: ");
		exit(errno);
	}

	ret = read(fd, sb, BLK_SZ);
//         ret = read_from_disk(fd, (char *)sb, BLK_SZ, sectornr);
	if (ret < 0) {
		perror("\n COuld not read the sb: ");
		exit(errno);
	}
	if (sb->magic != STL_SB_MAGIC) {
		printf("\n wrong superblock!");
		exit(-1);
	}
//	printf("\n sb->magic: %d", sb->magic);
//	printf("\n sb->version %d", sb->version);
//	printf("\n sb->log_sector_size %d", sb->log_sector_size);
//	printf("\n sb->log_block_size %d", sb->log_block_size);
//	printf("\n sb->blk_count_ckpt %d", sb->blk_count_ckpt);
//	//printf("\n sb-> %d", sb->);
//	printf("\n sb->zone_count: %d", sb->zone_count);

	printf("\n sb->magic: %d", sb->magic);
    printf("\n sb->version %d", sb->version);
    printf("\n sb->log_sector_size %d", sb->log_sector_size);
    printf("\n sb->log_block_size %d", sb->log_block_size);
    printf("\n sb->blk_count_ckpt %d", sb->blk_count_ckpt);
    printf("\n sb->log_zone_size: %d", sb->log_zone_size);
    printf("\n sb->checksum_offset %d", sb->checksum_offset);
    printf("\n sb->zone_count %d", sb->zone_count);
    printf("\n sb->blk_count_revmap_bm %d", sb->blk_count_revmap_bm);
    printf("\n sb->blk_count_revmap %d", sb->blk_count_revmap);

    printf("\n sb->blk_count_tm: %d", sb->blk_count_tm);
    printf("\n sb->blk_count_sit %d", sb->blk_count_sit);
    printf("\n sb->zone_count_reserved %d", sb->zone_count_reserved);
    printf("\n sb->zone_count_main %d", sb->zone_count_main);
    printf("\n sb->revmap_pba %d", sb->revmap_pba);
    printf("\n sb->tm_pba: %d", sb->tm_pba);
    printf("\n sb->revmap_bm_pba %d", sb->revmap_bm_pba);
    printf("\n sb->ckpt2_pba %d", sb->ckpt2_pba);
    printf("\n sb->sit_pba %d",sb->sit_pba);
    printf("\n sb->order_revmap_bm %d", sb->order_revmap_bm);
    printf("\n sb->zone0_pba %d", sb->zone0_pba);
    printf("\n sb->max_pba %d", sb->max_pba);
    printf("\n sb->crc %d", sb->crc);

	printf("\n Read verified!!!");
	printf("\n ==================== \n");
	free(sb);
}

__le64 get_max_pba(struct stl_sb *sb)
{
	return sb->zone_count * (1 << (sb->log_zone_size - sb->log_sector_size));

}

void read_revmap(int fd, sector_t revmap_pba, unsigned nr_blks)
{
   struct stl_revmap_entry_sector * revmap_sector;
   int ret = 0;
   revmap_sector = (struct stl_revmap_entry_sector *) malloc(BLK_SZ);

   for(unsigned int i=0; i<nr_blks; i++){
     ret = read_from_disk(fd, (char *) revmap_sector, BLK_SZ, revmap_pba);
     for(unsigned int j=0; j< NR_SECTORS_IN_BLK; j++){
     //for this function print which data ??
       printf("\n *********************\n");

       for(int k=0; k< NR_EXT_ENTRIES_PER_SEC_for_Debug; k++){
        printf("\n revmap_sector->crc: %lld", revmap_sector->extents[k].lba);
        printf("\n revmap_sector->extend[k].pba: %lld", revmap_sector->extents[k].pba);
        printf("\n revmap_sector->extend[k].len: %d", revmap_sector->extents[k].len);

       }
       printf("\n revmap_sector->crc: %d", revmap_sector->crc);
       revmap_sector++;

    }
  }

}

void read_tm(int fd, sector_t tm_pba, unsigned nr_blks)
{
	printf("\n Writing tm blocks at pba: %llu, nrblks: %u", tm_pba/NR_SECTORS_IN_BLK, nr_blks);
	 struct tm_entry * tm_entry_ptr;
	 int ret = 0;

       tm_entry_ptr = (struct tm_entry *) malloc(BLK_SZ);

       for(unsigned int i=0; i<nr_blks; i++){
         ret = read_from_disk(fd, (char *) tm_entry_ptr, BLK_SZ, tm_pba);
         for(unsigned int j=0; j< 8; j++){
         //for this function print which data ??
           printf("\n *********************\n");
           printf("\n tm_entry_ptr->lba: %lld", tm_entry_ptr->lba);
           printf("\n tm_entry_ptr->pba: %lld", tm_entry_ptr->pba);
           tm_entry_ptr++;

        }
      }
}


/* Revmap bitmap: 0 indicates blk is available for writing
 */
void read_revmap_bitmap(int fd, sector_t revmap_bm_pba, unsigned nr_blks)
{

	   struct stl_revmap_bitmaps *lrb;
	   lrb = (struct stl_revmap_bitmaps *) malloc(BLK_SZ);
	   int ret =0;
//	   char *ptr;
//     ptr = (char *) malloc(BLK_SZ);
       ret = read_from_disk(fd, (char *) lrb, BLK_SZ, revmap_bm_pba);
       //print which data?
//       prinf()
       printf("\n *********************\n");
       for(int i=0; i< 16384; i++){
          printf("\n stl_revmap_bitmaps->bitmap0: %d", lrb->bitmap0[i]);
       }

       for(int j=0; j< 16384; j++){
           printf("\n stl_revmap_bitmaps->bitmap1: %d", lrb->bitmap1[j]);
            }

       free(lrb);

}

unsigned long long get_current_frontier(struct stl_sb *sb)
{
	unsigned long sit_end_pba = sb->sit_pba + sb->blk_count_sit * NR_SECTORS_IN_BLK;
	unsigned long sit_end_blk_nr = sit_end_pba / NR_SECTORS_IN_BLK;
	unsigned int sit_zone_nr = sit_end_blk_nr / NR_BLKS_PER_ZONE;
	if (sit_end_blk_nr % NR_BLKS_PER_ZONE > 0) {
		sit_zone_nr = sit_zone_nr + 1;
	}

	/* The data zones start in the next zone of that of the last
	 * metadata zone
	 */
	printf("\n sit_end_pba: %ld", sit_end_pba);
	printf("\n sit_end_blk_nr: %ld", sit_end_blk_nr);
	printf("\n sit_zone_nr: %d", sit_zone_nr);
	return (sit_zone_nr + 1) * (1 << (sb->log_zone_size - sb->log_sector_size));
}

unsigned long long get_current_gc_frontier(struct stl_sb *sb)
{
	int zonenr = get_zone_count();

	zonenr = zonenr - 20;
	return (zonenr) * (1 << (sb->log_zone_size - sb->log_sector_size));
}


__le64 get_lba(unsigned int zonenr, unsigned int blknr)
{
	int nrblks = (zonenr * NR_BLKS_PER_ZONE) + blknr;
	return (nrblks * NR_SECTORS_IN_BLK);
}

void set_bitmap(char *bitmap, unsigned int nrzones, char ch)
{
	int nrbytes = nrzones / 8;
	if (nrzones % 8 > 0)
		nrbytes = nrbytes + 1;
	memset(bitmap, ch, nrbytes);
}



unsigned long long get_user_block_count(struct stl_sb *sb)
{
	unsigned long sit_end_pba = sb->sit_pba + sb->blk_count_sit * NR_SECTORS_IN_BLK;
	unsigned long sit_zone_nr = sit_end_pba >> sb->log_zone_size;
	return ((sb->zone_count - sit_zone_nr) * (NR_BLKS_PER_ZONE));

}

void prepare_cur_seg_entry(struct stl_seg_entry *entry)
{
	/* Initially no block has any valid data */
	entry->vblocks = 0;
	entry->mtime = 0;
}


void prepare_prev_seg_entry(struct stl_seg_entry *entry)
{
	entry->vblocks = 0;
	entry->mtime = 0;
}

/* YUXIN: read the structure stl_ckpt found in format_metadata.h*/
void read_ckpt(int fd, struct stl_sb * sb, unsigned long ckpt_pba)
{
	struct stl_ckpt *ckpt;
	unsigned int bitmap_sz = sb->zone_count / BITS_IN_BYTE;

	ckpt = (struct stl_ckpt *)malloc(BLK_SZ);
	if(!ckpt)
		exit(-1);

	memset(ckpt, 0, BLK_SZ);
	read_from_disk(fd, (char *)ckpt, BLK_SZ, ckpt_pba);
	printf("\n checkpoint: cur_frontier_pba: %lld", ckpt->cur_frontier_pba);
	printf("\n ----------------------------------- \n ");
	printf("\n ckpt: ");
//	printf("\n ckpt->magic: %d", ckpt->magic);
//    printf("\n ckpt->version %lld", ckpt->version);
//    printf("\n ckpt->log_sector_size %d", ckpt->log_sector_size);
//    printf("\n ckpt->log_block_size %d", ckpt->log_block_size);
//    printf("\n ckpt->blk_count_ckpt %d", ckpt->blk_count_ckpt);
//	printf("\n ckpt->log_zone_size: %d", ckpt->log_zone_size);
//    printf("\n ckpt->checksum_offset %d", ckpt->checksum_offset);
//    printf("\n ckpt->zone_count %d", ckpt->zone_count);
//    printf("\n ckpt->blk_count_revmap_bm %d", ckpt->blk_count_revmap_bm);
//    printf("\n ckpt->blk_count_revmap %d", ckpt->blk_count_revmap);
//
//    printf("\n ckpt->blk_count_tm: %d", ckpt->blk_count_tm);
//    printf("\n ckpt->blk_count_sit %d", ckpt->blk_count_sit);
//    printf("\n ckpt->zone_count_reserved %d", ckpt->zone_count_reserved);
//    printf("\n ckpt->zone_count_main %d", ckpt->zone_count_main);
//    printf("\n ckpt->revmap_pba %d", ckpt->revmap_pba);
//    printf("\n ckpt->tm_pba: %d", ckpt->tm_pba);
//    printf("\n ckpt->revmap_bm_pba %d", ckpt->revmap_bm_pba);
//    printf("\n ckpt->ckpt2_pba %d", ckpt->ckpt2_pba);
//    printf("\n ckpt->sit_pba %d", ckpt->sit_pba);
//    printf("\n ckpt->order_revmap_bm %d", ckpt->order_revmap_bm);
//    printf("\n ckpt->zone0_pba %d", ckpt->zone0_pba);
//    printf("\n ckpt->max_pba %d", ckpt->max_pba);
//    printf("\n ckpt->crc %d", ckpt->crc);
    printf("\n ckpt->magic: %d", ckpt->magic);
    printf("\n ckpt->version: %lld", ckpt->version);
    printf("\n ckpt->user_block_count: %lld", ckpt->user_block_count);
    printf("\n ckpt->nr_invalid_zones: %d", ckpt->nr_invalid_zones);
    printf("\n ckpt->cur_frontier_pba: %lld", ckpt->cur_frontier_pba);
    printf("\n ckpt->nr_free_zones: %lld", ckpt->nr_free_zones);
    printf("\n ckpt->elapsed_time: %lld", ckpt->elapsed_time);
    printf("\n ckpt->clean: %d", ckpt->clean);
    printf("\n ckpt->crc: %lld", ckpt->crc);
    printf("\n ckpt->padding[0]: %d", ckpt->padding[0]);


	free(ckpt);
}


void read_seg_info_table(int fd, u64 nr_seg_entries, unsigned long seg_entries_pba)
{
	struct stl_seg_entry seg_entry;
	unsigned entries_in_blk = BLK_SZ / sizeof(struct stl_seg_entry);
	unsigned int nr_sit_blks = (nr_seg_entries)/entries_in_blk;
	unsigned int i, ret;
	char * buf;
	char *orig_addr;
	size_t size = 0;

	if (nr_seg_entries % entries_in_blk > 0)
		nr_sit_blks = nr_sit_blks + 1;


	buf = (char *) malloc(BLK_SZ);
	if (NULL == buf) {
		perror("\n Could not malloc: ");
		exit(-ENOMEM);
	}

	memset(buf, 0, BLK_SZ);

	printf("\n nr of seg entries: %llu", nr_seg_entries);
	printf("\n nr of sit blks: %d", nr_sit_blks);
	printf("\n");


	i = 0;
	buf = orig_addr;
	while (i < nr_sit_blks) {
		ret = read_from_disk(fd, (char *) buf, BLK_SZ, seg_entries_pba);
		if (ret < 0) {
			exit(ret);
		}
		seg_entries_pba += BLK_SZ;
		i++;
		for (number of seg entries in the blk) {
			seg_entry_ptr = (struct seg_entry * ) buf
			printf("\n seg_entry.vblocks: %d", seg_entry_ptr->vblocks);
			printf("\n seg_entry.mtime: %lld", seg_entry_ptr->mtime);
		}
	}

	free(buf);
}

void menu(int fd, unsigned long sectornr, sector_t revmap_pba, unsigned nr_blks, sector_t revmap_bm_pba, unsigned nr_blks_for_bitmap, u64 nr_seg_entries, unsigned long seg_entries_pba){
	int menuNum;
	printf("1. Printing_SB\n");
    printf("2. Printing_Revmap\n");
    printf("3. Printing_Seg_info_table\n");
    printf("4. Printing_Revmap_BitMap\n");
    printf("Input 1-4: ");
    scanf("%d",&menuNum);

    switch(menuNum)
    {
	case 1: 
		read_sb(fd,sectornr);
		break;
    	case 2: 
		read_revmap(fd, revmap_pba,nr_blks);
		break;
	case 3: 
		read_seg_info_table(fd, nr_seg_entries, seg_entries_pba);
		break;
    	case 4: 
		read_revmap_bitmap(fd, revmap_bm_pba,nr_blks_for_bitmap);
		break;
    	default:
		printf("Wrong input!\n");
		exit(0);
    }
}


/*
 *
 * SB1, SB2, Revmap, Translation Map, Revmap Bitmap, CKPT1, CKPT2, SIT, Dataa
 *
 */

int main()
{
	unsigned int pba = 0;
	struct stl_sb *sb1, *sb2;
	char cmd[256];
	unsigned long nrblks;
	unsigned int ret = 0;

	char * blkdev = "/dev/vdb";
	int fd = open_disk(blkdev);
	sb1 = (struct stl_sb *)malloc(BLK_SZ);

	menu(fd,0,sb1->revmap_pba,sb1->blk_count_revmap,sb1->revmap_bm_pba,sb1->blk_count_revmap_bm,sb1->zone_count, sb1->sit_pba);




	/*
	read_sb(fd, 0);
	read_sb(fd, 8);
	read_ckpt(fd, sb1, sb1->ckpt1_pba);
	read_ckpt(fd, sb1, sb1->ckpt2_pba);
    	read_revmap(fd, sb1->revmap_pba, sb1->blk_count_revmap);
	nrblks = get_nr_blks(sb1);
	printf("\n nrblks: %lu", nrblks);
	printf("\n nrblks: %lu", nrblks);
	read_tm(fd, sb1->tm_pba, sb1->blk_count_tm);
	read_revmap_bitmap(fd, sb1->revmap_bm_pba, sb1->blk_count_revmap_bm);
	read_ckpt(fd, sb1, sb1->ckpt1_pba);
	printf("\n Checkpoint written at offset: %d", sb1->ckpt1_pba);
	read_ckpt(fd, sb1, sb1->ckpt2_pba);
	printf("\n Extent map written");
	printf("\n sb1->zone_count: %d", sb1->zone_count);
	read_seg_info_table(fd, sb1->zone_count, sb1->sit_pba);
	printf("\n Segment Information Table written");
	pba = 0;
	read_ckpt(fd, sb1, sb1->ckpt1_pba);
	free(sb1);
	close(fd);
	*/
	/* 0 volume_size: 39321600  lsdm  blkdev: /dev/vdb tgtname: TL1 zone_lbas: 524288 data_end: 41418752 */
	/*
	unsigned long zone_lbas = 524288;
	unsigned long data_zones = 28000;
	char * tgtname = "TL1";
	//volume_size = data_zones * zone_lbas;
	unsigned long volume_size = 39321600;
	unsigned long data_end = 41418752;
	sprintf(cmd, "/sbin/dmsetup create TL1 --table '0 %ld lsdm %s %s %ld %ld'",
            volume_size, blkdev, tgtname, zone_lbas,
	    data_end);
	printf("\n cmd: %s", cmd);
    	//system(cmd);
	*/
	printf("\n \n");
	return(0);
}
