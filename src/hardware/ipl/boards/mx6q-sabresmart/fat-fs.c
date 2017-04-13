/*
 * $QNXLicenseC:
 * Copyright 2011, QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "ipl.h"
#include "sdmmc.h"
#include "fat-fs.h"

/* file system information structure */
fs_info_t        fs_info;

/* common block buffer */
static unsigned         blk_buf[2*SECT_SIZE/sizeof(unsigned)];
static unsigned char    *blk;

/* memory copy */
static inline void
mem_cpy(void *dst, const void *src, int length )
{
    const char *s = src;
    char       *d = dst;

    while (length > 0)
    {
        *d++ = *s++;
        length --;
    }
}

/* memory length */
static inline int 
strlen(const char *str)
{
    const char *ch;

    for (ch = str; *ch; ++ch);

    return(ch - str);
}

/* reads a sector relative to the start of the block device */
static inline int
read_sect(mx6x_sdmmc_t *sdmmc, unsigned blkno, void *buf, unsigned blkcnt)
{
    return sdmmc_read(sdmmc, buf, blkno, blkcnt);
}

/* reads a sector relative to the start of the partition 0 */
static inline int
read_fsect(unsigned sector, void *buf, unsigned sect_cnt)
{
    return read_sect((mx6x_sdmmc_t *)fs_info.device,
                        sector + fs_info.fs_offset, buf, sect_cnt);
}

/* detects the type of FAT */
static inline int
fat_detect_type(bpb_t *bpb)
{
    bpb32_t   *bpb32 = (bpb32_t *)bpb;
   
    if (GET_WORD(bpb->sig) != 0xaa55)
    {
        ser_putstr("BPB signature is wrong\n");
        return -1;
    }
   
    {
        int rc, bs, ns;

        bs = GET_WORD(bpb->sec_size);
        rc = GET_WORD(bpb->num_root_ents) * 32 + bs - 1;
        for (ns = 0; rc >= bs; ns++)
            rc -= bs;
        fs_info.root_dir_sectors = ns;
    }
   
    fs_info.fat_size = GET_WORD(bpb->num16_fat_secs);
    if (fs_info.fat_size == 0)
        fs_info.fat_size = GET_LONG(bpb32->num32_fat_secs);
   
    fs_info.total_sectors = GET_WORD(bpb->num16_secs);
    if (fs_info.total_sectors == 0)
        fs_info.total_sectors = GET_LONG(bpb->num32_secs);
   
    fs_info.number_of_fats = bpb->num_fats;
    fs_info.reserved_sectors = GET_WORD(bpb->num_rsvd_secs);
   
    fs_info.data_sectors = fs_info.total_sectors -
        (fs_info.reserved_sectors + fs_info.number_of_fats * fs_info.fat_size + fs_info.root_dir_sectors);
   
    fs_info.cluster_size = bpb->sec_per_clus;

    {
        int ds, cs, nc;

        ds = fs_info.data_sectors;
        cs = fs_info.cluster_size;
        for (nc = 0; ds >= cs; nc++)
            ds -= cs;
        fs_info.count_of_clusters = nc;
    }
   
    fs_info.root_entry_count = GET_WORD(bpb->num_root_ents);
    fs_info.fat1_start = fs_info.reserved_sectors;
    fs_info.fat2_start = fs_info.fat1_start + fs_info.fat_size;
    fs_info.root_dir_start = fs_info.fat2_start + fs_info.fat_size;
    fs_info.cluster2_start = fs_info.root_dir_start + ((fs_info.root_entry_count * 32) + (512 - 1)) / 512;

    if (fs_info.count_of_clusters < 4085) {
        
        return 12;

    } else if (fs_info.count_of_clusters < 65525) {
        
        return 16;

    } else {
        
        fs_info.root_dir_start = GET_LONG(bpb32->root_clus);
        return 32;
    }

    return 0;
}

/* reads the Master Boot Record to get FAT information */
int
fat_read_mbr(mx6x_sdmmc_t *sdmmc, int verbose)
{
    mbr_t           *mbr;
    bpb_t           *bpb;
    partition_t     *pe;
    unsigned short  sign;

    blk = (unsigned char *)blk_buf;
    mbr = (mbr_t *)&blk[0];
    bpb = (bpb_t *)&blk[SECT_SIZE];
    pe  = (partition_t *)&(mbr->part_entry[0]);

    /* read MBR from sector 0 */
    if (SDMMC_OK != read_sect(sdmmc, 0, mbr, 1)) {
        return SDMMC_ERROR;
    }

    if ((sign = mbr->sign) != 0xaa55) {
        ser_putstr("   Error: MBR signature (");  ser_puthex((unsigned int)sign);   ser_putstr(") is wrong\n");
        return SDMMC_ERROR;
    }
   
    if (GET_LONG(pe->part_size) == 0) {
        ser_putstr("   Error: No information in partition 0.\n");
        return SDMMC_ERROR;
    }
      
    /* read BPB structure */
    fs_info.device = sdmmc;
    fs_info.fs_offset = GET_LONG(pe->part_offset);
    if (read_fsect(0, (unsigned char *)bpb, 1)) {
        ser_putstr("   Error: cannot read BPB\n");
        return SDMMC_ERROR;
    }

    /* detect the FAT type of partition 0 */
    if (-1 == (fs_info.fat_type = fat_detect_type(bpb))) {
        ser_putstr("   error detecting BPB type\n");
        return SDMMC_ERROR;
    }

    if (verbose) {
        ser_putstr("Partition entry 0:");

        ser_putstr("      Boot Indicator: ");
            ser_puthex((unsigned int)mbr->part_entry[0].boot_ind); ser_putstr("\n");

        ser_putstr("      FAT type:       ");
            ser_puthex((unsigned int)fs_info.fat_type); ser_putstr("\n");

        ser_putstr("      Begin C_H_S:    "); 
            ser_puthex((unsigned int)pe->beg_head); ser_putstr(" ");
            ser_puthex((unsigned int)pe->begin_sect); ser_putstr(" ");
            ser_puthex((unsigned int)pe->beg_cylinder); ser_putstr("\n ");

        ser_putstr("      Type:           ");
            ser_puthex((unsigned int)pe->os_type); ser_putstr("\n");

        ser_putstr("      END C_H_S:      "); 
            ser_puthex((unsigned int)pe->end_head); ser_putstr(" ");
            ser_puthex((unsigned int)pe->end_sect); ser_putstr(" ");
            ser_puthex((unsigned int)pe->end_cylinder); ser_putstr("\n ");

        ser_putstr("      Start:          ");
            ser_puthex((unsigned int)GET_LONG(pe->part_offset)); ser_putstr("\n");

        ser_putstr("      Size:           ");
            ser_puthex((unsigned int)GET_LONG(pe->part_size)); ser_putstr("\n");
    }

    return SDMMC_OK;
}

/* converts a cluster number into sector numbers to the partition 0 */
static inline unsigned
clust2fsect(unsigned clust)
{
    return fs_info.cluster2_start + (clust - 2) * fs_info.cluster_size;
}

/* gets the entry of the FAT for the given cluster number */
static inline unsigned
fat_get_fat_entry(unsigned clust)
{
    /* We only support FAT32 now */
    unsigned        fat_sec   = fs_info.fat1_start + ((clust * 4) / SECT_SIZE);
    unsigned        fat_offs  = (clust * 4) % SECT_SIZE;
    unsigned char   *data_buf = (unsigned char *)blk_buf;

    if (SDMMC_OK != read_fsect(fat_sec, data_buf, 1))
        return 0;

    return (*(unsigned long *)(data_buf + fat_offs)) & 0x0fffffff;
}

/* checks for end of file condition */
static inline int
end_of_file (unsigned clust)
{
    /* We only support FAT32 for now */
    return ((clust == 0x0ffffff8) || (clust == 0x0fffffff));
}

/* read a cluster */
static inline int
read_clust(unsigned clust, unsigned char *buf, int size)
{
    unsigned sectors = (size / SECT_SIZE) >= fs_info.cluster_size ?
                            fs_info.cluster_size : size / SECT_SIZE;
    unsigned rest    = (size / SECT_SIZE) >= fs_info.cluster_size ?
                            0 : size % SECT_SIZE;
   
    if (sectors) {
        if (SDMMC_OK != read_fsect(clust2fsect(clust), buf, sectors)) {
            return SDMMC_ERROR;
        }
    }
      
    if (rest) {
        if (SDMMC_OK != read_fsect(clust2fsect(clust) + sectors, blk, 1)) {
            return SDMMC_ERROR;
        }
        mem_cpy(buf + sectors * SECT_SIZE, blk, rest);
    }

    return SDMMC_OK;
}

/*  copy a file from to a memory location */ 
int
fat_copy_file(unsigned clust, unsigned size, unsigned char *buf)
{
    int     sz  = (int)size;
   
    while(!end_of_file(clust) && (sz > 0)) {
        int txf = MIN(sz, fs_info.cluster_size * SECT_SIZE);

        if (SDMMC_OK != read_clust(clust, buf, txf)) {
            return SDMMC_ERROR;
        }
         
        sz   -= txf;
        buf  += txf;
        clust = fat_get_fat_entry(clust);
    }

    return SDMMC_OK;
}

/* copy file by name (FAT32) */
int
fat_copy_named_file(unsigned char *buf, char *name)
{
    unsigned    dir_clust = fs_info.root_dir_start;
    unsigned    clust_sz  = fs_info.cluster_size;
    unsigned    dir_sect;
    int         i, len;
    int         ent = 0; 
  
    while(!end_of_file(dir_clust)) {
        int sub_sect = 0;
        dir_sect = clust2fsect(dir_clust);

        while(sub_sect < clust_sz) {
            if (SDMMC_OK != read_fsect(dir_sect + sub_sect, blk, 1)) {
                return SDMMC_ERROR;
            }

            ent = 0; 
            while (ent < SECT_SIZE / sizeof(dir_entry_t)) {
                dir_entry_t *dir_entry = (dir_entry_t *)&(blk[ent * sizeof(dir_entry_t)]);

                if (dir_entry->short_name[0] == ENT_END)
                    break;

                if (dir_entry->short_name[0] == ENT_UNUSED) {
                    ent++;
                    continue;
                }

                len = strlen (name) < 11 ? strlen (name) : 11;
                for (i = 0; i < len; i++) {
                    if (name[i] != dir_entry->short_name[i])
                        break;
                }

                if ((i < len) || (GET_CLUSTER(dir_entry) == 0)) {
                    ent++;
                    continue;
                }

                return fat_copy_file(GET_CLUSTER(dir_entry), dir_entry->size, buf);
            }

            if (ent < SECT_SIZE / sizeof(dir_entry_t))
                break;

            sub_sect++;
        }

        dir_clust = fat_get_fat_entry(dir_clust);
    }

    return SDMMC_ERROR;
}

