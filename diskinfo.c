#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
struct BootSector {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_dir_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
};
#pragma pack(pop)

uint32_t get_fat_entry(uint8_t *fat, uint32_t cluster) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint16_t fat_entry = *(uint16_t*)&fat[fat_offset];
    if (cluster & 1) {
        return fat_entry >> 4;
    } else {
        return fat_entry & 0x0FFF;
    }
}

void count_files_recursive(FILE *file, uint32_t cluster, struct BootSector *bs, uint8_t *fat, uint32_t *file_count) {
    uint32_t sector;
    uint8_t dir_entry[32];

    if (cluster == 0) {
        // Root directory
        sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16;
    } else {
        sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16 + 
                 (bs->root_dir_entries * 32 + bs->bytes_per_sector - 1) / bs->bytes_per_sector +
                 (cluster - 2) * bs->sectors_per_cluster;
    }

    uint32_t entries_to_read = (cluster == 0) ? bs->root_dir_entries : (bs->bytes_per_sector / 32 * bs->sectors_per_cluster);

    for (uint32_t i = 0; i < entries_to_read; i++) {
        fseek(file, sector * bs->bytes_per_sector + i * 32, SEEK_SET);
        fread(dir_entry, sizeof(dir_entry), 1, file);

        if (dir_entry[0] == 0) break;  // End of directory
        if (dir_entry[0] == 0xE5) continue;  // Deleted entry

        uint16_t first_cluster = *(uint16_t*)&dir_entry[26];
        uint8_t attributes = dir_entry[11];

        if (attributes & 0x08) continue;  // Volume label, skip

        if (attributes & 0x10) {  // Subdirectory
            // Skip '.' and '..' entries
            if (dir_entry[0] != '.' && first_cluster != 0 && first_cluster != 1) {
                count_files_recursive(file, first_cluster, bs, fat, file_count);
            }
        } else {  // Regular file
            if (first_cluster != 0 && first_cluster != 1) {
                (*file_count)++;
            }
        }
    }
}

void get_volume_label(FILE *file, struct BootSector *bs, char *label) {
    // First, check the boot sector
    if (bs->volume_label[0] != 0 && bs->volume_label[0] != ' ') {
        strncpy(label, bs->volume_label, 11);
        label[11] = '\0';
        return;
    }

    // If not found in boot sector, search in root directory
    uint32_t root_dir_start = (bs->reserved_sectors + bs->num_fats * bs->fat_size_16) * bs->bytes_per_sector;
    uint8_t dir_entry[32];

    for (uint32_t i = 0; i < bs->root_dir_entries; i++) {
        fseek(file, root_dir_start + i * 32, SEEK_SET);
        fread(dir_entry, sizeof(dir_entry), 1, file);

        if (dir_entry[11] == 0x08) {  // Volume label attribute
            strncpy(label, (char*)dir_entry, 11);
            label[11] = '\0';
            return;
        }
    }

    // If still not found, set to "NO NAME"
    strcpy(label, "NO NAME    ");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    struct BootSector bs;
    fread(&bs, sizeof(bs), 1, file);

    printf("OS Name: %.8s\n", bs.oem);
    
    char volume_label[12];
    get_volume_label(file, &bs, volume_label);
    printf("Label of the disk: %s\n", volume_label);
    
    uint32_t total_sectors = bs.total_sectors_16 ? bs.total_sectors_16 : bs.total_sectors_32;
    uint32_t total_size = total_sectors * bs.bytes_per_sector;
    printf("Total size of the disk: %u bytes\n", total_size);

    uint32_t root_dir_sectors = ((bs.root_dir_entries * 32) + (bs.bytes_per_sector - 1)) / bs.bytes_per_sector;
    uint32_t fat_size = bs.fat_size_16;
    uint32_t first_data_sector = bs.reserved_sectors + (bs.num_fats * fat_size) + root_dir_sectors;
    uint32_t data_sectors = total_sectors - first_data_sector;
    uint32_t total_clusters = data_sectors / bs.sectors_per_cluster;

    uint8_t *fat = malloc(fat_size * bs.bytes_per_sector);
    fseek(file, bs.reserved_sectors * bs.bytes_per_sector, SEEK_SET);
    fread(fat, fat_size * bs.bytes_per_sector, 1, file);

    uint32_t free_clusters = 0;
    for (uint32_t i = 2; i < total_clusters + 2; i++) {
        if (get_fat_entry(fat, i) == 0) {
            free_clusters++;
        }
    }

    uint32_t free_size = free_clusters * bs.sectors_per_cluster * bs.bytes_per_sector;
    printf("Free size of the disk: %u bytes\n", free_size);
    printf("=============\n");

    uint32_t file_count = 0;
    count_files_recursive(file, 0, &bs, fat, &file_count);
    
    printf("The number of files in the disk: %u\n", file_count);
    printf("Number of FAT copies: %u\n", bs.num_fats);
    printf("Sectors per FAT: %u\n", bs.fat_size_16);

    free(fat);
    fclose(file);
    return 0;
}
