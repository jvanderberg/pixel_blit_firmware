/* hw_config.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/
/*

This file should be tailored to your hardware configuration.

*/

#include "hw_config.h"
#include "diskio.h" 

/* Configuration of RP2350 SPI object */
static spi_t sd_spi = {
    .hw_inst = spi1,  // SPI component (macro from SDK)
    .miso_gpio = 36,  // GPIO number (not pin number)
    .mosi_gpio = 39,
    .sck_gpio = 38,
    .baud_rate = 12500 * 1000, // 12.5 MHz (Start slow, increase later)
    //.baud_rate = 25 * 1000 * 1000, // 25 MHz
};

/* Configuration of the SD Card socket */
static sd_card_t sd_card = {
    .pcName = "0:",           // Name used to mount device
    .spi = &sd_spi,           // Pointer to the SPI structure
    .ss_gpio = 37,            // The Chip Select GPIO
    .use_card_detect = false, // No Card Detect pin on Pixel Blit
    .card_detect_gpio = -1,   
    .card_detected_true = -1, 
    .m_Status = STA_NOINIT
};

/* Dependencies */
size_t sd_get_num() { return 1; }

sd_card_t *sd_get_by_num(size_t num) {
    if (num == 0) return &sd_card;
    return NULL;
}

size_t spi_get_num() { return 1; }

spi_t *spi_get_by_num(size_t num) {
    if (num == 0) return &sd_spi;
    return NULL;
}
