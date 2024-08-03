// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/stat.h>
// #include <time.h>

// #define BYTES_PER_SECTOR 512

// // Structs based on FAT12 specification
// typedef struct {
//     unsigned char filename[8];
//     unsigned char ext[3];
//     unsigned char attributes;
//     unsigned char reserved[10];
//     unsigned short modify_time;
//     unsigned short modify_date;
//     unsigned short starting_cluster;
//     unsigned int file_size;
// } DirectoryEntry;

// typedef struct __attribute__((__packed__)) { 
//     unsigned char jmp[3];
//     char oem[8];
//     unsigned short sector_size;
//     unsigned char sectors_per_cluster;
//     unsigned short reserved_sectors;
//     unsigned char num_fats;
//     unsigned short root_dir_entries;
//     unsigned short total_sectors_short; // 2 bytes
//     unsigned char media_descriptor;
//     unsigned short fat_size_sectors;
//     unsigned short sectors_per_track;
//     unsigned short num_heads;
//     unsigned int hidden_sectors;
//     unsigned int total_sectors_long;  // 4 bytes
//     unsigned char drive_number;
//     unsigned char current_head;
//     unsigned char boot_signature;
//     unsigned int volume_id;
//     char volume_label[11];
//     char fs_type[8];
//     char boot_code[448];
//     unsigned short boot_sector_signature;
// } BootSector;

// unsigned int get_fat_entry(FILE *fp, int n) {
//     // Read FAT entry based on cluster number n
//     unsigned int fat_offset = (n * 3) / 2;
//     unsigned short val;
//     fseek(fp, 512 + fat_offset, SEEK_SET);
//     fread(&val, 2, 1, fp);
    
//     if (n & 0x0001) {
//         return val >> 4;
//     } else {
//         return val & 0x0FFF;
//     }
// }

// void set_fat_entry(FILE *fp, int n, unsigned int val) {
//     // Set FAT entry for cluster n to value val
//     unsigned int fat_offset = (n * 3) / 2;
//     unsigned short curVal, newVal;

//     fseek(fp, 512 + fat_offset, SEEK_SET);
//     fread(&curVal, 2, 1, fp);

//     if (n & 0x0001) {
//         newVal = (curVal & 0x000F) | (val << 4);
//     } else {
//         newVal = (curVal & 0xF000) | (val & 0x0FFF);
//     }

//     fseek(fp, 512 + fat_offset, SEEK_SET);
//     fwrite(&newVal, 2, 1, fp);
// }

// int main(int argc, char *argv[]) {
//     if (argc != 3) {
//         printf("Usage: ./diskput <disk_image> <file_to_copy>\n");
//         return 1;
//     }

//     char *disk_image = argv[1];
//     char *file_to_copy = argv[2];
    
//     // Open disk image file
//     FILE *fp = fopen(disk_image, "r+");
//     if (fp == NULL) {
//         printf("Failed to open disk image.\n");
//         return 1;
//     }

//     // Read boot sector
//     BootSector bs;
//     fread(&bs, sizeof(BootSector), 1, fp);

//     // Extract key info from boot sector 
//     int root_dir_sectors = ((bs.root_dir_entries * 32) + (bs.sector_size - 1)) / bs.sector_size;
//     int first_data_sector = bs.reserved_sectors + (bs.num_fats * bs.fat_size_sectors) + root_dir_sectors;
//     // int first_fat_sector = bs.reserved_sectors;
    
//     // Open source file to copy
//     FILE *src = fopen(file_to_copy, "rb");
//     if (src == NULL) {
//         printf("File not found.\n");
//         fclose(fp);
//         return 1;
//     }

//     // Get file size
//     fseek(src, 0L, SEEK_END);
//     int file_size = ftell(src);
//     rewind(src);

//     // Check if enough free space
//     int free_clusters = 0;
//     for (int i = 2; i < bs.fat_size_sectors * bs.sector_size / 2; i++) {
//         if (get_fat_entry(fp, i) == 0) {
//             free_clusters++;
//         }
//     }
//     int free_space = free_clusters * bs.sectors_per_cluster * BYTES_PER_SECTOR;

//     if (file_size > free_space) {
//         printf("Not enough free space in the disk image.\n");
//         fclose(fp);
//         fclose(src);
//         return 1;
//     }

//     // Find free directory entry
//     DirectoryEntry dir_entry;
//     int free_entry = -1;
//     int root_dir_start = (bs.reserved_sectors + bs.num_fats * bs.fat_size_sectors) * BYTES_PER_SECTOR;

//     for (int i = 0; i < bs.root_dir_entries; i++) {
//         fseek(fp, root_dir_start + i * sizeof(DirectoryEntry), SEEK_SET);
//         fread(&dir_entry, sizeof(DirectoryEntry), 1, fp);

//         if (dir_entry.filename[0] == 0x00 || dir_entry.filename[0] == 0xE5) {
//             free_entry = i;
//             break;
//         }
//     }

//     if (free_entry == -1) {
//         printf("Root directory is full.\n");
//         fclose(fp);
//         fclose(src);
//         return 1;
//     }

//     // Find free clusters for file data
//     int num_clusters_needed = (file_size + bs.sectors_per_cluster * BYTES_PER_SECTOR - 1) / (bs.sectors_per_cluster * BYTES_PER_SECTOR);
//     int first_cluster = 0;
//     int prev_cluster = 0;

//     for (int i = 2; i < bs.fat_size_sectors * bs.sector_size / 2; i++) {
//         if (get_fat_entry(fp, i) == 0) {
//             if (first_cluster == 0) {
//                 first_cluster = i;
//             }
//             if (prev_cluster != 0) {
//                 set_fat_entry(fp, prev_cluster, i);
//             }
//             prev_cluster = i;
//             num_clusters_needed--;

//             if (num_clusters_needed == 0) {
//                 set_fat_entry(fp, i, 0xFFF);
//                 break;
//             }
//         }
//     }

//     // Write file data to clusters
//     char *filename = strrchr(file_to_copy, '/');
//     if (!filename) {
//         filename = file_to_copy;
//     } else {
//         filename++;
//     }

//     char name[12];
//     memset(name, ' ', 11);
//     name[11] = '\0';

//     char *dot = strchr(filename, '.');
//     if (dot) {
//         int name_len = dot - filename;
//         if (name_len > 8) name_len = 8;
//         memcpy(name, filename, name_len);

//         int ext_len = strlen(dot+1);
//         if (ext_len > 3) ext_len = 3;
//         memcpy(name+8, dot+1, ext_len);
//     } else {
//         int name_len = strlen(filename);
//         if (name_len > 8) name_len = 8;
//         memcpy(name, filename, name_len);
//     }

//     int cluster = first_cluster;
//     while (file_size > 0) {
//         int sector = first_data_sector + (cluster - 2) * bs.sectors_per_cluster;
//         int offset = 0;
//         while (offset < bs.sectors_per_cluster * BYTES_PER_SECTOR && file_size > 0) {
//             char buffer[BYTES_PER_SECTOR];
//             int bytes_read = fread(buffer, 1, BYTES_PER_SECTOR, src);
//             fseek(fp, sector * BYTES_PER_SECTOR + offset, SEEK_SET);
//             fwrite(buffer, 1, bytes_read, fp);
//             offset += BYTES_PER_SECTOR;
//             file_size -= bytes_read;
//         }
//         cluster = get_fat_entry(fp, cluster);
//     }

//     // Update directory entry
//     struct stat file_stat;
//     stat(file_to_copy, &file_stat);

//     dir_entry.filename[0] = name[0];
//     memcpy(dir_entry.filename+1, name+1, 7);
//     memcpy(dir_entry.ext, name+8, 3);
//     dir_entry.attributes = 0x20;
//     dir_entry.reserved[0] = 0;
//     dir_entry.modify_time = file_stat.st_mtime;
//     dir_entry.modify_date = file_stat.st_mtime;
//     dir_entry.starting_cluster = first_cluster;
//     dir_entry.file_size = ftell(src);

//     fseek(fp, root_dir_start + free_entry * sizeof(DirectoryEntry), SEEK_SET);
//     fwrite(&dir_entry, sizeof(DirectoryEntry), 1, fp);

//     // Close files
//     fclose(fp);
//     fclose(src);

//     printf("File copied successfully.\n");
//     return 0;
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define BYTES_PER_SECTOR 512

// Structs based on FAT12 specification
typedef struct {
    unsigned char filename[8];
    unsigned char ext[3];
    unsigned char attributes;
    unsigned char reserved[10];
    unsigned short modify_time;
    unsigned short modify_date;
    unsigned short starting_cluster;
    unsigned int file_size;
} DirectoryEntry;

typedef struct __attribute__((__packed__)) {
    unsigned char jmp[3];
    char oem[8];
    unsigned short sector_size;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char num_fats;
    unsigned short root_dir_entries;
    unsigned short total_sectors_short;
    unsigned char media_descriptor;
    unsigned short fat_size_sectors;
    unsigned short sectors_per_track;
    unsigned short num_heads;
    unsigned int hidden_sectors;
    unsigned int total_sectors_long;
    unsigned char drive_number;
    unsigned char current_head;
    unsigned char boot_signature;
    unsigned int volume_id;
    char volume_label[11];
    char fs_type[8];
    char boot_code[448];
    unsigned short boot_sector_signature;
} BootSector;

unsigned int get_fat_entry(FILE *fp, int n) {
    unsigned int fat_offset = (n * 3) / 2;
    unsigned short val;
    fseek(fp, 512 + fat_offset, SEEK_SET);
    fread(&val, 2, 1, fp);
    if (n & 0x0001) {
        return val >> 4;
    } else {
        return val & 0x0FFF;
    }
}

void set_fat_entry(FILE *fp, int n, unsigned int val) {
    unsigned int fat_offset = (n * 3) / 2;
    unsigned short curVal, newVal;
    fseek(fp, 512 + fat_offset, SEEK_SET);
    fread(&curVal, 2, 1, fp);
    if (n & 0x0001) {
        newVal = (curVal & 0x000F) | (val << 4);
    } else {
        newVal = (curVal & 0xF000) | (val & 0x0FFF);
    }
    fseek(fp, 512 + fat_offset, SEEK_SET);
    fwrite(&newVal, 2, 1, fp);
}

int find_directory_entry(FILE *fp, int start_sector, const char *name, BootSector bs, DirectoryEntry *dir_entry, int *next_cluster) {
    int sector = start_sector;
    unsigned long entries_per_sector = bs.sector_size / sizeof(DirectoryEntry);
    int max_sectors = (start_sector == bs.reserved_sectors + bs.num_fats * bs.fat_size_sectors) ? 
                      bs.root_dir_entries / entries_per_sector : 
                      bs.sectors_per_cluster;

    for (int s = 0; s < max_sectors; s++) {
        for (unsigned long i = 0; i < entries_per_sector; i++) {
            fseek(fp, sector * BYTES_PER_SECTOR + i * sizeof(DirectoryEntry), SEEK_SET);
            fread(dir_entry, sizeof(DirectoryEntry), 1, fp);
            
            char entry_name[9];
            strncpy(entry_name, (char *)dir_entry->filename, 8);
            entry_name[8] = '\0';
            strtok(entry_name, " "); // Remove trailing spaces

            if (strcmp(entry_name, name) == 0 && (dir_entry->attributes & 0x10)) {
                *next_cluster = dir_entry->starting_cluster;
                return 1;
            }
        }
        sector++;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./diskput <disk_image> <file_to_copy>\n");
        return 1;
    }

    char *disk_image = argv[1];
    char *file_to_copy = argv[2];

    // Open disk image file
    FILE *fp = fopen(disk_image, "r+");
    if (fp == NULL) {
        printf("Failed to open disk image.\n");
        return 1;
    }

    // Read boot sector
    BootSector bs;
    fread(&bs, sizeof(BootSector), 1, fp);

    // Extract key info from boot sector
    int root_dir_sectors = ((bs.root_dir_entries * 32) + (bs.sector_size - 1)) / bs.sector_size;
    int first_data_sector = bs.reserved_sectors + (bs.num_fats * bs.fat_size_sectors) + root_dir_sectors;

    // Open source file to copy
    FILE *src = fopen(file_to_copy, "rb");
    if (src == NULL) {
        printf("File not found.\n");
        fclose(fp);
        return 1;
    }

    // Get file size
    fseek(src, 0L, SEEK_END);
    int file_size = ftell(src);
    rewind(src);

    // Check if enough free space
    int free_clusters = 0;
    for (int i = 2; i < bs.fat_size_sectors * bs.sector_size / 2; i++) {
        if (get_fat_entry(fp, i) == 0) {
            free_clusters++;
        }
    }
    int free_space = free_clusters * bs.sectors_per_cluster * BYTES_PER_SECTOR;
    if (file_size > free_space) {
        printf("No enough free space in the disk image.\n");
        fclose(fp);
        fclose(src);
        return 1;
    }

    // Parse directory path and check if it exists
    char *dir_path = strdup(file_to_copy);
    char *filename = strrchr(dir_path, '/');
    int start_cluster = 0; // Root directory
    if (filename) {
        *filename = '\0';
        filename++;
        char *token = strtok(dir_path + 1, "/"); // Skip leading '/'
        DirectoryEntry dir_entry;
        int next_cluster;
        while (token != NULL) {
            if (!find_directory_entry(fp, (start_cluster == 0 ? (bs.reserved_sectors + bs.num_fats * bs.fat_size_sectors) : (first_data_sector + (start_cluster - 2) * bs.sectors_per_cluster)), token, bs, &dir_entry, &next_cluster)) {
                printf("The directory not found.\n");
                fclose(fp);
                fclose(src);
                free(dir_path);
                return 1;
            }
            start_cluster = next_cluster;
            token = strtok(NULL, "/");
        }
    } else {
        filename = file_to_copy;
    }

    // Find free directory entry in the target directory
    DirectoryEntry dir_entry;
    int free_entry = -1;
    int dir_start_sector = (start_cluster == 0) ? (bs.reserved_sectors + bs.num_fats * bs.fat_size_sectors) : (first_data_sector + (start_cluster - 2) * bs.sectors_per_cluster);
    int entries_to_check = (start_cluster == 0) ? bs.root_dir_entries : (bs.sectors_per_cluster * bs.sector_size / sizeof(DirectoryEntry));

    for (int i = 0; i < entries_to_check; i++) {
        fseek(fp, dir_start_sector * BYTES_PER_SECTOR + i * sizeof(DirectoryEntry), SEEK_SET);
        fread(&dir_entry, sizeof(DirectoryEntry), 1, fp);
        if (dir_entry.filename[0] == 0x00 || dir_entry.filename[0] == 0xE5) {
            free_entry = i;
            break;
        }
    }

    if (free_entry == -1) {
        printf("Directory is full.\n");
        fclose(fp);
        fclose(src);
        free(dir_path);
        return 1;
    }

    // Find free clusters for file data
    int num_clusters_needed = (file_size + bs.sectors_per_cluster * BYTES_PER_SECTOR - 1) / (bs.sectors_per_cluster * BYTES_PER_SECTOR);
    int first_cluster = 0;
    int prev_cluster = 0;
    for (int i = 2; i < bs.fat_size_sectors * bs.sector_size / 2; i++) {
        if (get_fat_entry(fp, i) == 0) {
            if (first_cluster == 0) {
                first_cluster = i;
            }
            if (prev_cluster != 0) {
                set_fat_entry(fp, prev_cluster, i);
            }
            prev_cluster = i;
            num_clusters_needed--;
            if (num_clusters_needed == 0) {
                set_fat_entry(fp, i, 0xFFF);
                break;
            }
        }
    }

    // Write file data to clusters
    char name[12];
    memset(name, ' ', 11);
    name[11] = '\0';
    char *dot = strchr(filename, '.');
    if (dot) {
        int name_len = dot - filename;
        if (name_len > 8) name_len = 8;
        memcpy(name, filename, name_len);
        int ext_len = strlen(dot+1);
        if (ext_len > 3) ext_len = 3;
        memcpy(name+8, dot+1, ext_len);
    } else {
        int name_len = strlen(filename);
        if (name_len > 8) name_len = 8;
        memcpy(name, filename, name_len);
    }

    int cluster = first_cluster;
    int remaining_size = file_size;
    while (remaining_size > 0) {
        int sector = first_data_sector + (cluster - 2) * bs.sectors_per_cluster;
        int offset = 0;
        while (offset < bs.sectors_per_cluster * BYTES_PER_SECTOR && remaining_size > 0) {
            char buffer[BYTES_PER_SECTOR];
            int bytes_read = fread(buffer, 1, BYTES_PER_SECTOR, src);
            fseek(fp, sector * BYTES_PER_SECTOR + offset, SEEK_SET);
            fwrite(buffer, 1, bytes_read, fp);
            offset += BYTES_PER_SECTOR;
            remaining_size -= bytes_read;
        }
        cluster = get_fat_entry(fp, cluster);
    }

    // Update directory entry
    struct stat file_stat;
    stat(file_to_copy, &file_stat);
    memset(&dir_entry, 0, sizeof(DirectoryEntry));
    memcpy(dir_entry.filename, name, 8);
    memcpy(dir_entry.ext, name+8, 3);
    dir_entry.attributes = 0x20;
    dir_entry.modify_time = file_stat.st_mtime;
    dir_entry.modify_date = file_stat.st_mtime;
    dir_entry.starting_cluster = first_cluster;
    dir_entry.file_size = file_size;
    fseek(fp, dir_start_sector * BYTES_PER_SECTOR + free_entry * sizeof(DirectoryEntry), SEEK_SET);
    fwrite(&dir_entry, sizeof(DirectoryEntry), 1, fp);

    // Close files
    fclose(fp);
    fclose(src);
    free(dir_path);
    printf("File copied successfully.\n");
    return 0;
}
