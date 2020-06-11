/*
  C
*/
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>

/*
  General
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

/*
  CRC
*/
#include <rom/crc.h>

/*
  ESP
*/
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

/*
  Drivers
*/
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include "esp_adc_cal.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/ledc.h"

/*
  Odroid
*/
#include "../../components/odroid/odroid_settings.h"
#include "../../components/odroid/odroid_system.h"
#include "../../components/odroid/odroid_sdcard.h"
#include "../../components/odroid/odroid_display.h"
#include "../../components/odroid/odroid_input.h"
#include "../../components/odroid/odroid_audio.h"

/*
  Sprites
*/
#include "../sprites/battery.h"
#include "../sprites/brightness.h"
#include "../sprites/characters.h"
#include "../sprites/folder.h"
#include "../sprites/icons.h"
#include "../sprites/logo.h"
#include "../sprites/logo3d.h"
#include "../sprites/media.h"
#include "../sprites/speaker.h"
#include "../sprites/toggle.h"
#include "../sprites/systems.h"
