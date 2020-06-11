#include "odroid_system.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/rtc_io.h"
#include "string.h"
#include "stdio.h"

static bool system_initialized = false;

ODROID_START_ACTION startAction = 0;
ODROID_SCALING scalingMode = ODROID_SCALING_FILL;
int8_t speedupEnabled = 0;
int8_t forceRedraw = 0;

void odroid_system_init(int app_id, int sampleRate, char **romPath)
{
    if (system_initialized) abort();

    printf("odroid_system_init: %d KB free\n", esp_get_free_heap_size() / 1024);

    odroid_settings_init(app_id);
    odroid_overlay_init();
    odroid_system_gpio_init();
    odroid_input_gamepad_init();
    odroid_input_battery_level_init();

    odroid_gamepad_state bootState = odroid_input_read_raw();
    if (bootState.values[ODROID_INPUT_MENU])
    {
        // Force return to menu to recover from ROM loading crashes
        odroid_system_application_set(0);
        esp_restart();
    }

    startAction = odroid_settings_StartAction_get();
    if (startAction == ODROID_START_ACTION_RESTART)
    {
        odroid_settings_StartAction_set(ODROID_START_ACTION_RESUME);
    }

    //sdcard init must be before LCD init
    esp_err_t sd_init = odroid_sdcard_open();

    ili9341_init();

    if (esp_reset_reason() == ESP_RST_PANIC)
    {
        odroid_overlay_alert("The emulator crashed");
        odroid_system_application_set(0);
        esp_restart();
    }

    if (esp_reset_reason() != ESP_RST_SW)
    {
        ili9341_blank_screen();
        //odroid_display_show_hourglass();
    }

    if (sd_init != ESP_OK)
    {
        odroid_display_show_error(ODROID_SD_ERR_NOCARD);
        odroid_system_halt();
    }

    *romPath = odroid_settings_RomFilePath_get();
    if (!*romPath || strlen(*romPath) < 4)
    {
        odroid_overlay_alert("ROM File not found!");
        odroid_system_halt();
    }

    odroid_audio_init(odroid_settings_AudioSink_get(), sampleRate);

    scalingMode = odroid_settings_Scaling_get();

    if (startAction == ODROID_START_ACTION_NETPLAY)
    {
        // odroid_netplay_init();
    }

    system_initialized = true;

    printf("odroid_system_init: System ready!\n");
}

void odroid_system_gpio_init()
{
    rtc_gpio_deinit(ODROID_GAMEPAD_IO_MENU);
    //rtc_gpio_deinit(GPIO_NUM_14);

    // blue led
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);

    // Disable LCD CD to prevent garbage
    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_5, 1);
}

void odroid_system_application_set(int slot)
{
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_MIN + slot,
        NULL);
    if (partition != NULL)
    {
        esp_err_t err = esp_ota_set_boot_partition(partition);
        if (err != ESP_OK)
        {
            printf("odroid_system_application_set: esp_ota_set_boot_partition failed.\n");
            abort();
        }
    }
}

void odroid_system_sleep()
{
    printf("odroid_system_sleep: Entered.\n");

    // Wait for button release
    odroid_gamepad_state joystick;
    odroid_input_gamepad_read(&joystick);

    while (joystick.values[ODROID_INPUT_MENU])
    {
        vTaskDelay(1);
        odroid_input_gamepad_read(&joystick);
    }

    vTaskDelay(100);
    esp_deep_sleep_start();
}

void odroid_system_halt()
{
    vTaskSuspendAll();
    while (1);
}

void odroid_system_led_set(int value)
{
    if (!system_initialized)
    {
        printf("odroid_system_init not called before use.\n");
        abort();
    }

    gpio_set_level(GPIO_NUM_2, value);
}
