#pragma once

#include <stdint.h>
#include <stdbool.h>

// Result codes for SD operations
typedef enum {
    SD_OPS_OK = 0,
    SD_OPS_MOUNT_FAILED,
    SD_OPS_OPENDIR_FAILED,
} sd_ops_result_t;

// Result of a scan operation
typedef struct {
    sd_ops_result_t result;
    uint8_t file_count;  // Number of .fseq files found (files stored in sd_file_list)
} sd_ops_scan_result_t;

// Scan the SD card for .fseq files
// Populates sd_file_list[] with filenames and returns result
sd_ops_scan_result_t sd_ops_scan_fseq_files(void);
