#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

/*
disklist.c - FAT12 File System Directory Listing Utility

This program reads a FAT12 file system image and displays the contents of
the root directory and all subdirectories. It traverses the directory structure,
listing files and subdirectories with their attributes, sizes, and creation times.
The program uses a breadth-first search approach to handle multi-layer directories.
*/

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

struct DirEntry {
    char filename[8];
    char extension[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t starting_cluster;
    uint32_t file_size;
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

void print_datetime(uint16_t date, uint16_t time, uint8_t tenths) {
    int year = ((date >> 9) & 0x7F) + 1980;
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hours = (time >> 11) & 0x1F;
    int minutes = (time >> 5) & 0x3F;
    int seconds = (time & 0x1F) * 2 + tenths / 100;
    printf("%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hours, minutes, seconds);
}

struct QueueItem {
    uint32_t cluster;
    char *path;
};

void list_directory(FILE *file, uint32_t initial_cluster, struct BootSector *bs, uint8_t *fat, const char *initial_path) {
    struct QueueItem *queue = NULL;
    size_t queue_size = 0, queue_capacity = 0;
    size_t front = 0;

    // Enqueue the initial directory
    queue = realloc(queue, (queue_capacity + 1) * sizeof(struct QueueItem));
    if (!queue) {
        fprintf(stderr, "Memory allocation error\n");
        return;
    }
    queue_capacity++;
    queue[queue_size].cluster = initial_cluster;
    queue[queue_size].path = strdup(initial_path);
    queue_size++;

    while (front < queue_size) {
        uint32_t cluster = queue[front].cluster;
        char *path = queue[front].path;
        front++;

        printf("\n%s\n===================\n", path);

        do {
            uint32_t sector, entries_to_read;
            if (cluster == 0) {
                sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16;
                entries_to_read = bs->root_dir_entries;
            } else {
                sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16 +
                         (bs->root_dir_entries * 32 + bs->bytes_per_sector - 1) / bs->bytes_per_sector +
                         (cluster - 2) * bs->sectors_per_cluster;
                entries_to_read = bs->bytes_per_sector / 32 * bs->sectors_per_cluster;
            }

            for (uint32_t i = 0; i < entries_to_read; i++) {
                struct DirEntry entry;
                if (fseek(file, sector * bs->bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET) != 0) {
                    fprintf(stderr, "Error seeking in file: %s\n", strerror(errno));
                    goto cleanup;
                }
                if (fread(&entry, sizeof(struct DirEntry), 1, file) != 1) {
                    if (feof(file)) {
                        break;  // End of file reached
                    }
                    fprintf(stderr, "Error reading directory entry: %s\n", strerror(errno));
                    goto cleanup;
                }

                if (entry.filename[0] == 0) break;  // End of directory
                if ((uint8_t)entry.filename[0] == 0xE5) continue;  // Deleted entry
                if (entry.attributes == 0x0F) continue;  // Long file name entry

                // Skip "." and ".." entries
                if (entry.filename[0] == '.' && (entry.filename[1] == ' ' || (entry.filename[1] == '.' && entry.filename[2] == ' '))) {
                    continue;
                }

                char filename[21];  // 8 + 3 + 1(dot) + 1(null terminator)
                snprintf(filename, sizeof(filename), "%.8s%.3s", entry.filename, entry.extension);
                // Remove trailing spaces
                for (int j = strlen(filename) - 1; j >= 0 && filename[j] == ' '; j--) {
                    filename[j] = '\0';
                }

                // Skip invalid entries
                if (entry.starting_cluster == 0 || entry.starting_cluster == 1) continue;

                if (entry.attributes & 0x10) {
                    printf("D %10s %-20s ", "", filename);
                } else {
                    printf("F %10u %-20s ", entry.file_size, filename);
                }
                print_datetime(entry.creation_date, entry.creation_time, entry.creation_time_tenths);
                printf("\n");

                // Enqueue subdirectories
                if ((entry.attributes & 0x10) && entry.starting_cluster >= 2) {
                    char *new_path = malloc(strlen(path) + strlen(filename) + 2);
                    if (!new_path) {
                        fprintf(stderr, "Memory allocation error\n");
                        goto cleanup;
                    }
                    sprintf(new_path, "%s/%s", path, filename);

                    queue = realloc(queue, (queue_capacity + 1) * sizeof(struct QueueItem));
                    if (!queue) {
                        fprintf(stderr, "Memory allocation error\n");
                        free(new_path);
                        goto cleanup;
                    }
                    queue_capacity++;
                    queue[queue_size].cluster = entry.starting_cluster;
                    queue[queue_size].path = new_path;
                    queue_size++;
                }
            }

            if (cluster == 0) break;  // Root directory is contiguous
            cluster = get_fat_entry(fat, cluster);
        } while (cluster < 0xFF8);  // Continue until end of cluster chain

        free(path);  // Free the path string after processing the directory
    }

cleanup:
    // Free any remaining paths in the queue
    for (size_t i = front; i < queue_size; i++) {
        free(queue[i].path);
    }
    free(queue);
}

int main(int argc, char *argv[]) {
    // Check if the correct number of command-line arguments is provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    // Open the disk image file in binary read mode
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    // Read the boot sector from the disk image
    struct BootSector bs;
    if (fread(&bs, sizeof(bs), 1, file) != 1) {
        fprintf(stderr, "Error reading boot sector: %s\n", strerror(errno));
        fclose(file);
        return 1;
    }

    // Calculate the size of the FAT (File Allocation Table)
    uint32_t fat_size = bs.fat_size_16;

    // Allocate memory for the FAT
    uint8_t *fat = malloc(fat_size * bs.bytes_per_sector);
    if (!fat) {
        fprintf(stderr, "Error allocating memory for FAT: %s\n", strerror(errno));
        fclose(file);
        return 1;
    }

    // Seek to the beginning of the FAT in the disk image
    if (fseek(file, bs.reserved_sectors * bs.bytes_per_sector, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to FAT: %s\n", strerror(errno));
        free(fat);
        fclose(file);
        return 1;
    }

    // Read the FAT from the disk image
    if (fread(fat, fat_size * bs.bytes_per_sector, 1, file) != 1) {
        fprintf(stderr, "Error reading FAT: %s\n", strerror(errno));
        free(fat);
        fclose(file);
        return 1;
    }

    // List the contents of the root directory and all subdirectories
    // The '0' argument represents the root directory (cluster 0)
    // The '/' argument represents the root path
    list_directory(file, 0, &bs, fat, "/");

    // Clean up: free allocated memory and close the file
    free(fat);
    fclose(file);

    return 0;
}

