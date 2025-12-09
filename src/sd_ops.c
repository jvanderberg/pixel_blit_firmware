#include "sd_ops.h"
#include "app_state.h"  // For sd_file_list, SD_MAX_FILES, SD_FILENAME_LEN

#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

#include <stdio.h>
#include <string.h>

sd_ops_scan_result_t sd_ops_scan_fseq_files(void) {
    sd_ops_scan_result_t result = {
        .result = SD_OPS_OK,
        .file_count = 0,
    };

    sd_card_t *sd = sd_get_by_num(0);
    printf("SD: Mounting...\n");
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);
    printf("SD: Mount Result = %d\n", fr);

    if (fr != FR_OK) {
        result.result = SD_OPS_MOUNT_FAILED;
        return result;
    }

    printf("SD: Reading directory...\n");
    DIR dir;
    FILINFO fno;
    fr = f_opendir(&dir, "/");

    if (fr != FR_OK) {
        printf("SD: OpenDir failed: %d\n", fr);
        result.result = SD_OPS_OPENDIR_FAILED;
        return result;
    }

    // Read up to SD_MAX_FILES .fseq files into static buffer
    uint8_t count = 0;

    while (count < SD_MAX_FILES) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        // Skip hidden files and macOS resource forks (._*)
        if (fno.fname[0] == '.') continue;

        // Check extension .fseq (case-insensitive)
        char *dot = strrchr(fno.fname, '.');
        if (dot) {
            if ((dot[1] == 'f' || dot[1] == 'F') &&
                (dot[2] == 's' || dot[2] == 'S') &&
                (dot[3] == 'e' || dot[3] == 'E') &&
                (dot[4] == 'q' || dot[4] == 'Q') &&
                dot[5] == 0) {

                // Copy filename to static buffer
                for (int i = 0; i < SD_FILENAME_LEN - 1 && fno.fname[i]; i++) {
                    sd_file_list[count][i] = fno.fname[i];
                    sd_file_list[count][i+1] = 0;
                }
                printf("SD: Found: %s\n", sd_file_list[count]);
                count++;
            }
        }
    }
    f_closedir(&dir);

    printf("SD: Total .fseq files: %d\n", count);
    result.file_count = count;
    return result;
}
