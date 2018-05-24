// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "rom/gpio.h"
#include "soc/gpio_reg.h"
#include "bootloader_config.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "sdkconfig.h"
#include "esp_image_format.h"
#include "soc/rtc_io_reg.h"
#include "soc/io_mux_reg.h"


#define GPIO_PIN_REG_26         IO_MUX_GPIO26_REG
#define BOOT_GPIO26             CONFIG_ESP_BOOT_GPIO26

static const char* TAG = "boot";

static esp_err_t select_image (esp_image_metadata_t *image_data);
static int selected_boot_partition(const bootloader_state_t *bs);
/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void call_start_cpu0()
{
    // 1. Hardware initialization
    if(bootloader_init() != ESP_OK){
        return;
    }

#if BOOT_GPIO26
    // SET GPIO26 as input
    // REG_CLR_BIT(GPIO_ENABLE_REG, BIT26);
    // rtc_gpio_deinit(GPIO_NUM_26);
    CLEAR_PERI_REG_MASK(RTC_IO_TOUCH_PAD3_REG, RTC_IO_TOUCH_PAD3_MUX_SEL_M);

    //gpio_pad_select_gpio(GPIO_NUM_26);
    PIN_FUNC_SELECT(GPIO_PIN_REG_26, PIN_FUNC_GPIO);
  
    //gpio_set_direction(GPIO_NUM_26, GPIO_MODE_INPUT);
    PIN_INPUT_ENABLE(GPIO_PIN_REG_26);
    REG_WRITE(GPIO_ENABLE_W1TC_REG, BIT26);
  
    //gpio_set_pull_mode(GPIO_NUM_26, GPIO_PULLUP_ONLY);
    REG_CLR_BIT(GPIO_PIN_REG_26, FUN_PD);
    REG_SET_BIT(GPIO_PIN_REG_26, FUN_PU);
#endif

    // 2. Select image to boot
    esp_image_metadata_t image_data;
    if(select_image(&image_data) != ESP_OK){
        return;
    }

    // 3. Loading the selected image
    bootloader_utility_load_image(&image_data);
}

// Selects image to boot
static esp_err_t select_image (esp_image_metadata_t *image_data)
{
    // 1. Load partition table
    bootloader_state_t bs = { 0 };
    if (!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return ESP_FAIL;
    }

    // 2. Select boot partition
    int boot_index = selected_boot_partition(&bs);
    if(boot_index == INVALID_INDEX) {
        return ESP_FAIL; // Unrecoverable failure (not due to corrupt ota data or bad partition contents)
    }

    // 3. Load the app image for booting
    if (!bootloader_utility_load_boot_image(&bs, boot_index, image_data)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
 * Selects a boot partition.
 * The conditions for switching to another firmware are checked.
 */
static int selected_boot_partition(const bootloader_state_t *bs)
{
    int boot_index = bootloader_utility_get_selected_boot_partition(bs);
    if (boot_index == INVALID_INDEX) {
        return boot_index; // Unrecoverable failure (not due to corrupt ota data or bad partition contents)
    } else {
#if BOOT_GPIO26
        // Check for reset to the factory firmware or for launch OTA[x] firmware.
        // Customer implementation.
        if ( REG_GET_BIT(GPIO_IN_REG, BIT26) == BIT26) {
				ESP_LOGE(TAG, "GPIO 26 PRESSED: Booting on FACTORY_INDEX\n");
            boot_index = FACTORY_INDEX;
        } 
#endif
    } 
    return boot_index;
}
