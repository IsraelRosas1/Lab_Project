/* This file contains functions that are not part of the visible interface.
 * So they are essentially helper functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simfs.h"

/* Internal helper functions first.
 */
int get_data_start_offset() {
    int metadata_size = (sizeof(fentry) * MAXFILES) + (sizeof(fnode) * MAXBLOCKS);
    int num_blocks = metadata_size / BLOCKSIZE;
    if (metadata_size % BLOCKSIZE != 0) {
        num_blocks++;
    }
    return num_blocks * BLOCKSIZE;
}

/* Helper to get the byte offset of a specific fentry index */
int get_fentry_offset(int index) {
    return index * sizeof(fentry);
}

/* Helper to get the byte offset of a specific fnode index */
int get_fnode_offset(int index) {
    int fentry_array_size = sizeof(fentry) * MAXFILES;
    return fentry_array_size + (index * sizeof(fnode));
}
/* Internal helper: Returns the fentry index of a filename, or -1 if not found */
int find_fentry(FILE *fp, char *filename) {
    fentry fe;
    for (int i = 0; i < MAXFILES; i++) {
        fseek(fp, get_fentry_offset(i), SEEK_SET);
        if (fread(&fe, sizeof(fentry), 1, fp) < 1) continue;
        if (fe.name[0] != '\0' && strcmp(fe.name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

/* Internal helper: Returns index of first free fnode, or -1 if none found */
int find_free_fnode(FILE *fp) {
    fnode node;
    for (int i = 0; i < MAXBLOCKS; i++) {
        fseek(fp, get_fnode_offset(i), SEEK_SET);
        if (fread(&node, sizeof(fnode), 1, fp) < 1) continue;
        if (node.blockindex < 0) {
            return i;
        }
    }
    return -1;
}

FILE *
openfs(char *filename, char *mode)
{
    FILE *fp;
    if((fp = fopen(filename, mode)) == NULL) {
        perror("openfs");
        exit(1);
    }
    return fp;
}

void
closefs(FILE *fp)
{
    if(fclose(fp) != 0) {
        perror("closefs");
        exit(1);
    }
}

/* File system operations: creating, deleting, reading from, and writing to files.
 */
void createfile(char *fsname, char *filename) {
    FILE *fp = openfs(fsname, "rb+");
    fentry fe;
    int free_index = -1;

    // 1. Scan for duplicates AND find the first free slot
    for (int i = 0; i < MAXFILES; i++) {
        fseek(fp, get_fentry_offset(i), SEEK_SET);
        if (fread(&fe, sizeof(fentry), 1, fp) < 1) {
            fprintf(stderr, "createfile error: read failed\n");
            closefs(fp);
            exit(1);
        }

        // Check for duplicates
        if (fe.name[0] != '\0' && strcmp(fe.name, filename) == 0) {
            fprintf(stderr, "createfile error: file '%s' already exists\n", filename);
            closefs(fp);
            exit(1);
        }
        
        // Mark first empty slot
        if (free_index == -1 && fe.name[0] == '\0') {
            free_index = i;
        }
    }

    // 2. Check if we found a slot
    if (free_index == -1) {
        fprintf(stderr, "createfile error: MAXFILES reached\n");
        closefs(fp);
        exit(1);
    }

    // 3. Initialize the new fentry
    memset(fe.name, '\0', 12);
    strncpy(fe.name, filename, 11);
    fe.size = 0;
    fe.firstblock = -1; 

    // 4. Write it back to the correct offset
    fseek(fp, get_fentry_offset(free_index), SEEK_SET);
    if (fwrite(&fe, sizeof(fentry), 1, fp) < 1) {
        fprintf(stderr, "createfile error: write failed\n");
        closefs(fp);
        exit(1);
    }

    closefs(fp);
}

void deletefile(char *fsname, char *filename) {
    FILE *fp = openfs(fsname, "rb+");
    
    // 1. Find the file entry
    int entry_idx = find_fentry(fp, filename);
    if (entry_idx == -1) {
        fprintf(stderr, "deletefile error: file '%s' not found\n", filename);
        closefs(fp);
        exit(1);
    }

    fentry fe;
    fseek(fp, get_fentry_offset(entry_idx), SEEK_SET);
    fread(&fe, sizeof(fentry), 1, fp);

    // 2. Clear data blocks (The Linked List)
    short current_fnode_idx = fe.firstblock;
    char zero_block[BLOCKSIZE];
    memset(zero_block, 0, BLOCKSIZE);

    while (current_fnode_idx != -1) {
        fnode node;
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fread(&node, sizeof(fnode), 1, fp);

        // Calculate where the data actually lives on the "disk"
        // and overwrite it with zeros for security
        int data_offset = get_data_start_offset() + (node.blockindex * BLOCKSIZE);
        fseek(fp, data_offset, SEEK_SET);
        fwrite(zero_block, BLOCKSIZE, 1, fp);

        // Free the fnode by making the blockindex negative again
        // Note: We use the absolute value to ensure we know which block it was
        short next_node_idx = node.nextblock;
        node.blockindex = -(abs(node.blockindex)); 
        node.nextblock = -1;

        // Write the freed fnode back to the metadata section
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fwrite(&node, sizeof(fnode), 1, fp);

        current_fnode_idx = next_node_idx;
    }

    // 3. Wipe the fentry so it can be reused
    memset(fe.name, '\0', 12);
    fe.size = 0;
    fe.firstblock = -1;
    
    fseek(fp, get_fentry_offset(entry_idx), SEEK_SET);
    fwrite(&fe, sizeof(fentry), 1, fp);

    closefs(fp);
}

void readfile(char *fsname, char *filename, int start, int length) {
    FILE *fp = openfs(fsname, "rb"); // Read-only is fine here

    int entry_idx = find_fentry(fp, filename);
    if (entry_idx == -1) {
        fprintf(stderr, "readfile error: file '%s' not found\n", filename);
        closefs(fp);
        exit(1);
    }

    fentry fe;
    fseek(fp, get_fentry_offset(entry_idx), SEEK_SET);
    fread(&fe, sizeof(fentry), 1, fp);

    // Error check: Ensure start/length are within file bounds
    if (start < 0 || start >= fe.size || (start + length) > fe.size) {
        fprintf(stderr, "readfile error: invalid offset or length\n");
        closefs(fp);
        exit(1);
    }

    // Traverse to the starting block
    short current_fnode_idx = fe.firstblock;
    int bytes_to_skip = start;

    while (bytes_to_skip >= BLOCKSIZE && current_fnode_idx != -1) {
        fnode node;
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fread(&node, sizeof(fnode), 1, fp);
        current_fnode_idx = node.nextblock;
        bytes_to_skip -= BLOCKSIZE;
    }

    // Read and print loop
    int bytes_read = 0;
    char buffer[BLOCKSIZE];
    while (bytes_read < length && current_fnode_idx != -1) {
        fnode node;
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fread(&node, sizeof(fnode), 1, fp);

        int data_offset = get_data_start_offset() + (node.blockindex * BLOCKSIZE) + bytes_to_skip;
        int chunk = (length - bytes_read < BLOCKSIZE - bytes_to_skip) ? 
                     length - bytes_read : BLOCKSIZE - bytes_to_skip;

        fseek(fp, data_offset, SEEK_SET);
        fread(buffer, 1, chunk, fp);
        fwrite(buffer, 1, chunk, stdout);

        bytes_read += chunk;
        bytes_to_skip = 0; // After first block, start at beginning of next block
        current_fnode_idx = node.nextblock;
    }
    
    printf("\n"); // Newline for readability
    closefs(fp);
}

void writefile(char *fsname, char *filename, int start, int length) {
    FILE *fp = openfs(fsname, "rb+");

    // 1. Find the file
    int entry_idx = find_fentry(fp, filename);
    if (entry_idx == -1) {
        fprintf(stderr, "writefile error: file '%s' not found\n", filename);
        closefs(fp);
        exit(1);
    }

    fentry fe;
    fseek(fp, get_fentry_offset(entry_idx), SEEK_SET);
    fread(&fe, sizeof(fentry), 1, fp);

    // 2. Validate: Cannot write past the current size (no holes allowed)
    if (start > fe.size) {
        fprintf(stderr, "writefile error: start offset beyond file size\n");
        closefs(fp);
        exit(1);
    }

    // 3. Read data from stdin into a temporary buffer
    char *data = malloc(length);
    if (fread(data, 1, length, stdin) < (size_t)length) {
        // Not necessarily an error, but good to check
    }

    // 4. Traverse/Allocate blocks
    int bytes_written = 0;
    int current_offset = 0;
    short *prev_node_next_ptr = &fe.firstblock; // Pointer to the index we need to update
    short current_fnode_idx = fe.firstblock;

    // While we still have data to write...
    while (bytes_written < length) {
        // If we reach the end of the chain but need to write more, allocate a block
        if (current_fnode_idx == -1) {
            int new_idx = find_free_fnode(fp);
            if (new_idx == -1) {
                fprintf(stderr, "writefile error: disk full\n");
                free(data); closefs(fp); exit(1);
            }
            
            current_fnode_idx = (short)new_idx;
            *prev_node_next_ptr = current_fnode_idx; // Link previous node (or fe.firstblock) to this new one

            fnode new_node;
            fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
            fread(&new_node, sizeof(fnode), 1, fp);
            new_node.blockindex = abs(new_node.blockindex); // Mark as used
            new_node.nextblock = -1;
            
            fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
            fwrite(&new_node, sizeof(fnode), 1, fp);

            // Per instructions: Initialize new data block to zeros
            char zero[BLOCKSIZE] = {0};
            fseek(fp, get_data_start_offset() + (new_node.blockindex * BLOCKSIZE), SEEK_SET);
            fwrite(zero, BLOCKSIZE, 1, fp);
        }

        // Now we have a valid fnode (current_fnode_idx)
        fnode node;
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fread(&node, sizeof(fnode), 1, fp);

        // Does the 'start' position fall within this block?
        if (current_offset + BLOCKSIZE > start) {
            int block_start_byte = (start > current_offset) ? (start - current_offset) : 0;
            int bytes_to_write_in_block = BLOCKSIZE - block_start_byte;
            if (bytes_to_write_in_block > (length - bytes_written)) {
                bytes_to_write_in_block = length - bytes_written;
            }

            int data_pos = get_data_start_offset() + (node.blockindex * BLOCKSIZE) + block_start_byte;
            fseek(fp, data_pos, SEEK_SET);
            fwrite(data + bytes_written, 1, bytes_to_write_in_block, fp);
            
            bytes_written += bytes_to_write_in_block;
            start += bytes_to_write_in_block; // Update start so next block begins at its byte 0
        }

        current_offset += BLOCKSIZE;
        prev_node_next_ptr = &node.nextblock; // Save pointer to this node's nextblock field
        
        // Before moving to the next block, save any changes to the current node 
        // (like if we updated its nextblock index)
        fseek(fp, get_fnode_offset(current_fnode_idx), SEEK_SET);
        fwrite(&node, sizeof(fnode), 1, fp);
        
        current_fnode_idx = node.nextblock;
    }

    // 5. Final Metadata Update
    int new_total_size = (start > fe.size) ? start : fe.size;
    fe.size = (unsigned short)new_total_size;
    fseek(fp, get_fentry_offset(entry_idx), SEEK_SET);
    fwrite(&fe, sizeof(fentry), 1, fp);

    free(data);
    closefs(fp);
}
// Signatures omitted; design as you wish.
