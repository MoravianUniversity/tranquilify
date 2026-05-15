#include "config.h"

#include <stdio.h>

#include <esp_system.h>
#include <esp_private/esp_clk.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>

void print_config() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    int cpu_freq = esp_clk_cpu_freq();
    printf("%s v%d.%d (model %d) with %d CPU cores @ %.2f MHz with %s%s%s%s\n",
            CONFIG_IDF_TARGET,
            chip_info.full_revision / 100,
            chip_info.full_revision % 100,
            chip_info.model,
            chip_info.cores,
            cpu_freq / 1000000.0,
            (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WIFI" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_IEEE802154) ? "/ETH" : ""
        );

    uint32_t size_flash_chip = 0;
    esp_flash_get_size(NULL, &size_flash_chip);
    printf("Flash: %.2fMB %s ", size_flash_chip / (1024.0 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    if (esp_flash_default_chip->read_mode == SPI_FLASH_SLOWRD) {
        printf("single I/O, some limits on speed\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_FASTRD) {
        printf("single I/O, no limit on speed\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_DOUT) {
        printf("dual I/O (data)\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_DIO) {
        printf("dual I/O (address & data)\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_QOUT) {
        printf("quad I/O (data)\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_QIO) {
        printf("quad I/O (address & data)\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_OPI_STR) {
        printf("octal+single I/O\n");
    } else if (esp_flash_default_chip->read_mode == SPI_FLASH_OPI_DTR) {
        printf("octal+dual I/O\n");
    } else {
        printf("unknown flash read mode\n");
    }

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    printf("Internal RAM: %d KB (%d KB largest free, %d KB total free)\n",
        (info.total_allocated_bytes + info.total_free_bytes) / 1024,
        info.largest_free_block / 1024, info.total_free_bytes / 1024
    );

    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    printf("PSRAM: %d KB (%d KB largest free, %d KB total free) %s\n",
        (info.total_allocated_bytes + info.total_free_bytes) / 1024,
        info.largest_free_block / 1024, info.total_free_bytes / 1024,
        (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "embedded" : "external"
    );

    printf("\n");
}
