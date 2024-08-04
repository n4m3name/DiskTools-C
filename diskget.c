#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/*
diskget.c - FAT12 File System File Extraction Utility

This program extracts a specified file from the root directory of a FAT12 file system image
and copies it to the current working directory. It parses the boot sector, navigates the 
root directory to find the file, and then follows the FAT chain to read and write the file contents.

Usage: ./diskget <disk_image> <filename>
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

int main(int argc, char *argv[]) {
    // Check command line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <disk_image> <filename>\n", argv[0]);
        return 1;
    }

    // Open the disk image
    FILE *disk = fopen(argv[1], "rb");
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

    // Calculate important offsets
    uint32_t root_dir_start = (bs.reserved_sectors + bs.num_fats * bs.fat_size_16) * bs.bytes_per_sector;
    uint32_t data_start = root_dir_start + bs.root_dir_entries * 32;

    // Seek to the start of the root directory
    if (fseek(disk, root_dir_start, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking to root directory: %s\n", strerror(errno));
        fclose(disk);
        return 1;
    }

    // Search for the file in the root directory
    struct DirEntry entry;
    int found = 0;
    for (int i = 0; i < bs.root_dir_entries; i++) {
        if (fread(&entry, sizeof(entry), 1, disk) != 1) {
            if (feof(disk)) {
                break;  // End of directory reached
            }
            fprintf(stderr, "Error reading directory entry: %s\n", strerror(errno));
            fclose(disk);
            return 1;
        }

        // Construct the filename (8.3 format)
        char filename[13];
        int j;
        // Copy filename, removing spaces
        for (j = 0; j < 8 && entry.filename[j] != ' '; j++) {
            filename[j] = tolower(entry.filename[j]);
        }
        // Add dot if extension exists
        if (entry.extension[0] != ' ') {
            filename[j++] = '.';
            for (int k = 0; k < 3 && entry.extension[k] != ' '; k++) {
                filename[j++] = tolower(entry.extension[k]);
            }
        }
        filename[j] = '\0';

        // Convert input filename to lowercase for comparison
        char input_filename[13];
        strncpy(input_filename, argv[2], sizeof(input_filename) - 1);
        input_filename[sizeof(input_filename) - 1] = '\0';
        for (int k = 0; input_filename[k]; k++) {
            input_filename[k] = tolower(input_filename[k]);
        }

        if (strcmp(filename, input_filename) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("File not found.\n");
        fclose(disk);
        return 1;
    }

    // Open the output file
    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror("Error creating output file");
        fclose(disk);
        return 1;
    }

    // Copy the file contents
    uint16_t current_cluster = entry.starting_cluster;
    uint32_t bytes_remaining = entry.file_size;
    uint32_t cluster_size = bs.sectors_per_cluster * bs.bytes_per_sector;
    char *buffer = malloc(cluster_size);
    if (!buffer) {
        fprintf(stderr, "Error allocating memory: %s\n", strerror(errno));
        fclose(output);
        fclose(disk);
        return 1;
    }

    while (current_cluster < 0xFF8 && bytes_remaining > 0) {
        uint32_t cluster_start = data_start + (current_cluster - 2) * cluster_size;
        if (fseek(disk, cluster_start, SEEK_SET) != 0) {
            fprintf(stderr, "Error seeking to cluster: %s\n", strerror(errno));
            free(buffer);
            fclose(output);
            fclose(disk);
            return 1;
        }

        uint32_t to_read = (bytes_remaining < cluster_size) ? bytes_remaining : cluster_size;
        size_t bytes_read = fread(buffer, 1, to_read, disk);
        if (bytes_read == 0 && ferror(disk)) {
            fprintf(stderr, "Error reading cluster: %s\n", strerror(errno));
            free(buffer);
            fclose(output);
            fclose(disk);
            return 1;
        }

        if (fwrite(buffer, 1, bytes_read, output) != bytes_read) {
            fprintf(stderr, "Error writing to output file: %s\n", strerror(errno));
            free(buffer);
            fclose(output);
            fclose(disk);
            return 1;
        }

        bytes_remaining -= bytes_read;
        current_cluster = read_fat_entry(disk, &bs, current_cluster);
    }

    // Clean up
    free(buffer);
    fclose(output);
    fclose(disk);
    printf("File copied successfully.\n");
    return 0;
}
