#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

/*
diskput.c - FAT12 File System File Insertion Utility

This program copies a file from the current Linux directory into a specified
directory (root or subdirectory) of a FAT12 file system image. It updates the
File Allocation Table (FAT) and directory entries accordingly. The program
handles file path parsing, directory traversal, free space checking, and 
cluster allocation to ensure proper file insertion into the FAT12 structure.
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
};

struct DirEntry {
    char filename[8];
    char extension[3];
    uint8_t attributes;
    uint8_t reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t starting_cluster;
    uint32_t file_size;
};
#pragma pack(pop)

// Function to read a FAT entry
uint16_t read_fat_entry(FILE *disk, struct BootSector *bs, uint16_t cluster) {
    uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector + cluster * 3 / 2;
    uint16_t fat_entry;

    if (fseek(disk, fat_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to FAT entry: %s\n", strerror(errno));
        return 0xFFF;  // Return an invalid cluster number
    }

    if (fread(&fat_entry, 2, 1, disk) != 1) {
        fprintf(stderr, "Error reading FAT entry: %s\n", strerror(errno));
        return 0xFFF;  // Return an invalid cluster number
    }

    // Extract the 12-bit FAT entry
    if (cluster & 1) {
        return fat_entry >> 4;
    } else {
        return fat_entry & 0x0FFF;
    }
}

// Function to write a FAT entry
void write_fat_entry(FILE *disk, struct BootSector *bs, uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector + cluster * 3 / 2;
    uint16_t fat_entry;

    if (fseek(disk, fat_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to FAT entry for writing: %s\n", strerror(errno));
        return;
    }

    if (fread(&fat_entry, 2, 1, disk) != 1) {
        fprintf(stderr, "Error reading FAT entry for writing: %s\n", strerror(errno));
        return;
    }

    // Update the 12-bit FAT entry
    if (cluster & 1) {
        fat_entry = (fat_entry & 0x000F) | (value << 4);
    } else {
        fat_entry = (fat_entry & 0xF000) | value;
    }

    if (fseek(disk, fat_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking back to FAT entry for writing: %s\n", strerror(errno));
        return;
    }

    if (fwrite(&fat_entry, 2, 1, disk) != 1) {
        fprintf(stderr, "Error writing FAT entry: %s\n", strerror(errno));
    }
}

// Function to find a free cluster
uint16_t find_free_cluster(FILE *disk, struct BootSector *bs) {
    for (uint16_t cluster = 2; cluster < bs->total_sectors_16 / bs->sectors_per_cluster; cluster++) {
        if (read_fat_entry(disk, bs, cluster) == 0) {
            return cluster;
        }
    }
    return 0xFFF;  // No free cluster found
}

// Function to find a directory given a path
uint16_t find_directory(FILE *disk, struct BootSector *bs, const char *path) {
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return 0;  // Special case for root directory
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        fprintf(stderr, "Memory allocation error: %s\n", strerror(errno));
        return 0xFFF;
    }

    char *token = strtok(path_copy, "/");
    uint16_t current_cluster = 0;  // Start from root directory

    while (token != NULL) {
        uint32_t dir_sector;
        uint32_t entries_to_read;

        if (current_cluster == 0) {
            // Root directory
            dir_sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16;
            entries_to_read = bs->root_dir_entries;
        } else {
            // Subdirectory
            dir_sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16 +
                         (bs->root_dir_entries * 32 + bs->bytes_per_sector - 1) / bs->bytes_per_sector +
                         (current_cluster - 2) * bs->sectors_per_cluster;
            entries_to_read = bs->bytes_per_sector / 32 * bs->sectors_per_cluster;
        }

        struct DirEntry entry;
        int found = 0;

        for (uint32_t i = 0; i < entries_to_read; i++) {
            if (fseek(disk, dir_sector * bs->bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET) != 0) {
                fprintf(stderr, "Error seeking to directory entry: %s\n", strerror(errno));
                free(path_copy);
                return 0xFFF;
            }

            if (fread(&entry, sizeof(entry), 1, disk) != 1) {
                if (feof(disk)) {
                    break;  // End of directory
                }
                fprintf(stderr, "Error reading directory entry: %s\n", strerror(errno));
                free(path_copy);
                return 0xFFF;
            }

            if (entry.filename[0] == 0) break;  // End of directory
            if ((uint8_t)entry.filename[0] == 0xE5) continue;  // Deleted entry

            char name[13];
            snprintf(name, sizeof(name), "%.8s%.3s", entry.filename, entry.extension);
            // Remove trailing spaces
            for (int j = 0; j < 12; j++) {
                if (name[j] == ' ') {
                    name[j] = '\0';
                    break;
                }
            }

            if (strcmp(name, token) == 0 && (entry.attributes & 0x10)) {
                current_cluster = entry.starting_cluster;
                found = 1;
                break;
            }
        }

        if (!found) {
            free(path_copy);
            return 0;  // Directory not found
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_cluster;
}

int main(int argc, char *argv[]) {
    // Check for correct number of command-line arguments
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s <disk_image> [/path/to/]<filename>\n", argv[0]);
        return 1;
    }

    // Open the disk image file in read-write binary mode
    FILE *disk = fopen(argv[1], "r+b");
    if (!disk) {
        perror("Error opening disk image");
        return 1;
    }

    // Read the boot sector
    struct BootSector bs;
    if (fread(&bs, sizeof(bs), 1, disk) != 1) {
        fprintf(stderr, "Error reading boot sector: %s\n", strerror(errno));
        fclose(disk);
        return 1;
    }

    // Parse the input path and filename
    char *filepath = (argc == 4) ? argv[3] : argv[2];
    char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    char dirpath[256] = {0};
    if (filename != filepath) {
        strncpy(dirpath, filepath, filename - filepath - 1);
    }

    // Find the target directory
    uint16_t dir_cluster = find_directory(disk, &bs, dirpath);
    if (dir_cluster == 0xFFF) {
        fclose(disk);
        return 1;  // Error already printed in find_directory
    }
    if (dir_cluster == 0 && dirpath[0] != '\0') {
        printf("The directory not found.\n");
        fclose(disk);
        return 1;
    }

    // Open the input file
    FILE *input_file = fopen(filename, "rb");
    if (!input_file) {
        printf("File not found.\n");
        fclose(disk);
        return 1;
    }

    // Get the file size
    if (fseek(input_file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error seeking in input file: %s\n", strerror(errno));
        fclose(input_file);
        fclose(disk);
        return 1;
    }
    long file_size = ftell(input_file);
    if (file_size == -1) {
        fprintf(stderr, "Error getting file size: %s\n", strerror(errno));
        fclose(input_file);
        fclose(disk);
        return 1;
    }
    if (fseek(input_file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking in input file: %s\n", strerror(errno));
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    // Calculate required clusters and check for free space
    uint32_t clusters_needed = (file_size + bs.bytes_per_sector * bs.sectors_per_cluster - 1) / (bs.bytes_per_sector * bs.sectors_per_cluster);
    uint32_t free_clusters = 0;
    for (uint16_t cluster = 2; cluster < bs.total_sectors_16 / bs.sectors_per_cluster; cluster++) {
        if (read_fat_entry(disk, &bs, cluster) == 0) {
            free_clusters++;
        }
    }

    if (free_clusters < clusters_needed) {
        printf("No enough free space in the disk image.\n");
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    // Find the target directory sector
    uint32_t dir_sector;
    uint32_t entries_to_read;
    if (dir_cluster == 0) {
        // Root directory
        dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16;
        entries_to_read = bs.root_dir_entries;
    } else {
        // Subdirectory
        dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16 +
                     (bs.root_dir_entries * 32 + bs.bytes_per_sector - 1) / bs.bytes_per_sector +
                     (dir_cluster - 2) * bs.sectors_per_cluster;
        entries_to_read = bs.bytes_per_sector / 32 * bs.sectors_per_cluster;
    }

    // Find a free directory entry
    struct DirEntry entry;
    int free_entry_index = -1;
    for (uint32_t i = 0; i < entries_to_read; i++) {
        if (fseek(disk, dir_sector * bs.bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET) != 0) {
            fprintf(stderr, "Error seeking to directory entry: %s\n", strerror(errno));
            fclose(input_file);
            fclose(disk);
            return 1;
        }

        if (fread(&entry, sizeof(entry), 1, disk) != 1) {
            fprintf(stderr, "Error reading directory entry: %s\n", strerror(errno));
            fclose(input_file);
            fclose(disk);
            return 1;
        }

        if (entry.filename[0] == 0x00 || (unsigned char)entry.filename[0] == 0xE5) {
            free_entry_index = i;
            break;
        }
    }

    if (free_entry_index == -1) {
        printf("No free directory entries.\n");
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    // Prepare the directory entry
    memset(&entry, 0, sizeof(entry));

    // Convert filename to uppercase and format it to 8.3
    char upper_filename[256];
    for (int i = 0; filename[i] && i < 255; i++) {
        upper_filename[i] = toupper((unsigned char)filename[i]);
    }
    upper_filename[255] = '\0';

    char *dot = strrchr(upper_filename, '.');
    char base_name[9] = "        ";  // 8 spaces
    char extension[4] = "   ";  // 3 spaces
    if (dot) {
        strncpy(base_name, upper_filename, (dot - upper_filename > 8) ? 8 : (dot - upper_filename));
        strncpy(extension, dot + 1, 3);
    } else {
        strncpy(base_name, upper_filename, 8);
    }

    memcpy(entry.filename, base_name, 8);
    memcpy(entry.extension, extension, 3);
    entry.attributes = 0x00;  // Regular file

    // Set creation time and date
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    entry.time = (tm_now->tm_hour << 11) | (tm_now->tm_min << 5) | (tm_now->tm_sec / 2);
    entry.date = ((tm_now->tm_year - 80) << 9) | ((tm_now->tm_mon + 1) << 5) | tm_now->tm_mday;
    entry.file_size = file_size;

    // Find the first free cluster and write the file
    uint16_t first_cluster = find_free_cluster(disk, &bs);
    if (first_cluster == 0xFFF) {
        fprintf(stderr, "No free clusters available.\n");
        fclose(input_file);
        fclose(disk);
        return 1;
    }
    entry.starting_cluster = first_cluster;

    uint16_t current_cluster = first_cluster;
    uint32_t bytes_written = 0;
    while (bytes_written < file_size) {
        uint32_t cluster_start = bs.reserved_sectors * bs.bytes_per_sector +
                                 bs.num_fats * bs.fat_size_16 * bs.bytes_per_sector +
                                 bs.root_dir_entries * 32 +
                                 (current_cluster - 2) * bs.sectors_per_cluster * bs.bytes_per_sector;

        uint32_t to_write = bs.sectors_per_cluster * bs.bytes_per_sector;
        if (to_write > file_size - bytes_written) {
            to_write = file_size - bytes_written;
        }

        char buffer[4096];
        size_t bytes_read = fread(buffer, 1, to_write, input_file);
        if (bytes_read != to_write) {
            fprintf(stderr, "Error reading from input file: %s\n", strerror(errno));
            fclose(input_file);
            fclose(disk);
            return 1;
        }

        if (fseek(disk, cluster_start, SEEK_SET) != 0) {
            fprintf(stderr, "Error seeking in disk image: %s\n", strerror(errno));
            fclose(input_file);
            fclose(disk);
            return 1;
        }

        if (fwrite(buffer, 1, to_write, disk) != to_write) {
            fprintf(stderr, "Error writing to disk image: %s\n", strerror(errno));
            fclose(input_file);
            fclose(disk);
            return 1;
        }

        bytes_written += to_write;

        if (bytes_written < file_size) {
            uint16_t next_cluster = find_free_cluster(disk, &bs);
            if (next_cluster == 0xFFF) {
                fprintf(stderr, "No more free clusters available.\n");
                fclose(input_file);
                fclose(disk);
                return 1;
            }
            write_fat_entry(disk, &bs, current_cluster, next_cluster);
            current_cluster = next_cluster;
        } else {
            write_fat_entry(disk, &bs, current_cluster, 0xFFF);  // End of file
        }
    }

    // Write the directory entry
    if (fseek(disk, dir_sector * bs.bytes_per_sector + free_entry_index * sizeof(struct DirEntry), SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to write directory entry: %s\n", strerror(errno));
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    if (fwrite(&entry, sizeof(entry), 1, disk) != 1) {
        fprintf(stderr, "Error writing directory entry: %s\n", strerror(errno));
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    // Clean up
    fclose(input_file);
    fclose(disk);
    printf("File copied successfully.\n");
    return 0;
}
