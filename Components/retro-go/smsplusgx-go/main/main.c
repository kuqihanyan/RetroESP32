#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_err.h"

#include "../components/smsplus/shared.h"
#include "odroid_system.h"

#ifdef CONFIG_IN_GAME_MENU_YES
    #include <dirent.h>
    #include "odroid_hud.h"
    int ACTION;
#endif

static odroid_gamepad_state previousJoystickState;

#define AUDIO_SAMPLE_RATE (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

#define SMS_WIDTH 256
#define SMS_HEIGHT 192

#define GG_WIDTH 160
#define GG_HEIGHT 144

#define PIXEL_MASK 0x1F
#define PAL_SHIFT_MASK 0x80

static char* romPath = NULL;

static uint32_t* audioBuffer;

static uint8_t* framebuffers[2];
static int currentBuffer = 0;

struct video_update {
    odroid_scanline diff[SMS_HEIGHT];
    uint8_t *buffer;
    uint16_t palette[PALETTE_SIZE];
    short width;
    short height;
    short stride;
};
static struct video_update update1 = {0,};
static struct video_update update2 = {0,};
static struct video_update *currentUpdate = &update1;

static uint totalElapsedTime = 0;
static uint emulatedFrames = 0;
static uint skippedFrames = 0;
static uint fullFrames = 0;

static bool skipFrame = false;

static bool consoleIsGG = false;
static bool consoleIsSMS = false;

volatile QueueHandle_t videoTaskQueue;
// --- MAIN


static void videoTask(void *arg)
{
    videoTaskQueue = xQueueCreate(1, sizeof(void*));

    int8_t previousScalingMode = -1;
    struct video_update* update;

    while(1)
    {
        xQueuePeek(videoTaskQueue, &update, portMAX_DELAY);

        if (!update) break;

        bool redraw = previousScalingMode != scalingMode || forceRedraw;
        if (redraw)
        {
            ili9341_blank_screen();
            previousScalingMode = scalingMode;
            forceRedraw = false;

            if (scalingMode) {
                float aspect = consoleIsGG && scalingMode == ODROID_SCALING_FILL ? 1.2f : 1.f;
                //float aspect = 0.5f;
                odroid_display_set_scale(update->width, update->height, aspect);
            } else {
                odroid_display_reset_scale(update->width, update->height);
            }
        }

        ili9341_write_frame_scaled(update->buffer, redraw ? NULL : update->diff,
                                   update->width, update->height, update->stride,
                                   1, PIXEL_MASK, update->palette);

        xQueueReceive(videoTaskQueue, &update, portMAX_DELAY);
    }

    videoTaskQueue = NULL;

    vTaskDelete(NULL);

    while (1) {}
}

void SaveState()
{
    // Save sram
    odroid_input_battery_monitor_enabled_set(0);
    odroid_system_led_set(1);

    odroid_display_lock();
    odroid_display_drain_spi();

    char* pathName = odroid_sdcard_get_savefile_path(romPath);
    if (!pathName) abort();

    FILE* f = fopen(pathName, "w");

    if (f == NULL)
    {
        printf("SaveState: fopen save failed\n");
        odroid_overlay_alert("Save failed");
    }
    else
    {
        system_save_state(f);
        fclose(f);

        printf("SaveState: system_save_state OK.\n");
    }

    odroid_display_unlock();

    free(pathName);

    odroid_system_led_set(0);
    odroid_input_battery_monitor_enabled_set(1);
}

void LoadState(const char* cartName)
{
    odroid_display_lock();
    odroid_display_drain_spi();

    char* pathName = odroid_sdcard_get_savefile_path(romPath);
    if (!pathName) abort();

    FILE* f = fopen(pathName, "r");
    if (f == NULL)
    {
        printf("LoadState: fopen load failed\n");
    }
    else
    {
        system_load_state(f);
        fclose(f);

        printf("LoadState: loadstate OK.\n");
    }

    odroid_display_unlock();

    free(pathName);
}

void QuitEmulator(bool save)
{
    printf("QuitEmulator: stopping tasks.\n");

    void *exitVideoTask = NULL;
    xQueueSend(videoTaskQueue, &exitVideoTask, portMAX_DELAY);
    while (videoTaskQueue) vTaskDelay(1);

    odroid_audio_terminate();
    ili9341_blank_screen();

    odroid_display_lock();
    odroid_display_show_hourglass();
    odroid_display_unlock();

    if (save) {
        printf("QuitEmulator: Saving state.\n");
        SaveState();
    }

    // Set menu application
    odroid_system_application_set(0);

    // Reset
    esp_restart();
}

void system_manage_sram(uint8 *sram, int slot, int mode)
{
    // printf("system_manage_sram\n");
    // sram_load();
}

void app_main(void)
{
    printf("smsplusgx (%s-%s).\n", COMPILEDATE, GITREV);

    // Do before odroid_system_init to make sure we get the caps requested
    framebuffers[0] = heap_caps_malloc(SMS_WIDTH * SMS_HEIGHT, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    framebuffers[1] = heap_caps_malloc(SMS_WIDTH * SMS_HEIGHT, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    audioBuffer     = heap_caps_calloc(AUDIO_BUFFER_LENGTH * 2, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

    // Init all the console hardware
    odroid_system_init(3, AUDIO_SAMPLE_RATE, &romPath);

   #ifdef CONFIG_IN_GAME_MENU_YES
      printf("\n\n%s\n%s\n\n", __func__, romPath);
      char* save_name = odroid_sdcard_get_filename(romPath);      
      hud_check_saves(odroid_sdcard_get_filename(romPath));
   #endif          

    assert(framebuffers[0] && framebuffers[1]);
    assert(audioBuffer);

    xTaskCreatePinnedToCore(&videoTask, "videoTask", 4096, NULL, 5, NULL, 1);


    // Load ROM
    load_rom(romPath);

    sms.use_fm = 0;

// #if 1
// 	sms.country = TYPE_OVERSEAS;
// #else
//     sms.country = TYPE_DOMESTIC;
// #endif

	//sms.dummy = framebuffer[0]; //A normal cart shouldn't access this memory ever. Point it to vram just in case.
	// sms.sram = malloc(SRAM_SIZE);
    // if (!sms.sram)
    //     abort();
    //
    // memset(sms.sram, 0xff, SRAM_SIZE);

    bitmap.width = SMS_WIDTH;
    bitmap.height = SMS_HEIGHT;
    bitmap.pitch = bitmap.width;
    //bitmap.depth = 8;
    bitmap.data = framebuffers[0];

    set_option_defaults();

    option.sndrate = AUDIO_SAMPLE_RATE;
    option.overscan = 0;
    option.extra_gg = 0;

    system_init2();
    system_reset();

    if (startAction == ODROID_START_ACTION_RESUME)
    {
        LoadState(romPath);
    }

    consoleIsSMS = sms.console == CONSOLE_SMS || sms.console == CONSOLE_SMS2;
    consoleIsGG  = sms.console == CONSOLE_GG || sms.console == CONSOLE_GGMS;

    uint8_t refresh = (sms.display == DISPLAY_NTSC) ? 60 : 50;
    const int frameTime = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000 / refresh;

    while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);

        if (joystick.values[ODROID_INPUT_MENU]) {
            #ifdef CONFIG_IN_GAME_MENU_YES
                odroid_display_lock();
                hud_menu(); 
                printf("\nACTION:%d", ACTION);
                void* arg = 1;
                switch(ACTION) {
                    case 3:
                    case 4:
                       hud_progress("Saving...", true);
                       hud_deinit();
                       SaveState();
                       esp_restart();
                    break;
                    case 5:
                       printf("\nDELETE ROM\n");
                    break;
                }


                odroid_display_unlock(); 
                /*
                ili9341_write_frame_scaled(currentUpdate->buffer, NULL, // NULL, //
                                           GB_WIDTH, GB_HEIGHT,
                                           GB_WIDTH * 2, 2, 0xFF, NULL);  
                                           */                        
            #else
                odroid_overlay_game_menu();
            #endif
        }
        else if (joystick.values[ODROID_INPUT_VOLUME]) {
            odroid_overlay_game_settings_menu(NULL, 0);
        }

        // Scaling
        if (joystick.values[ODROID_INPUT_START] && !previousJoystickState.values[ODROID_INPUT_RIGHT] && joystick.values[ODROID_INPUT_RIGHT])
        {
           uint8_t level = odroid_settings_Scaling_get();
           uint8_t max = 2;

           printf("\n CHANGE SCALE: %d \n", level);

           level++;
           if(level > max) {level = 0;}

           scalingMode = level;  
           odroid_settings_Scaling_set(level);
            //scaling_enabled = !scaling_enabled;
            //odroid_settings_ScaleDisabled_set(ODROID_SCALE_DISABLE_NES, scaling_enabled ? 0 : 1);
        }           

        uint startTime = xthal_get_ccount();

        input.pad[0] = 0;
        input.system = 0;

    	if (joystick.values[ODROID_INPUT_UP])    input.pad[0] |= INPUT_UP;
    	if (joystick.values[ODROID_INPUT_DOWN])  input.pad[0] |= INPUT_DOWN;
    	if (joystick.values[ODROID_INPUT_LEFT])  input.pad[0] |= INPUT_LEFT;
    	if (joystick.values[ODROID_INPUT_RIGHT]) input.pad[0] |= INPUT_RIGHT;
    	if (joystick.values[ODROID_INPUT_A])     input.pad[0] |= INPUT_BUTTON2;
    	if (joystick.values[ODROID_INPUT_B])     input.pad[0] |= INPUT_BUTTON1;

		if (consoleIsSMS)
		{
			if (joystick.values[ODROID_INPUT_START])  input.system |= INPUT_PAUSE;
			if (joystick.values[ODROID_INPUT_SELECT]) input.system |= INPUT_START;
		}
		else if (consoleIsGG)
		{
			if (joystick.values[ODROID_INPUT_START])  input.system |= INPUT_START;
			if (joystick.values[ODROID_INPUT_SELECT]) input.system |= INPUT_PAUSE;
		}
        else // Coleco
        {
            coleco.keypad[0] = 0xff;
            coleco.keypad[1] = 0xff;

            if (joystick.values[ODROID_INPUT_SELECT])
            {
                odroid_input_wait_for_key(ODROID_INPUT_SELECT, false);
                system_reset();
            }

            // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, *, #
            switch (cart.crc)
            {
                case 0x798002a2:    // Frogger
                case 0x32b95be0:    // Frogger
                case 0x9cc3fabc:    // Alcazar
                case 0x964db3bc:    // Fraction Fever
                    if (joystick.values[ODROID_INPUT_START])
                    {
                        coleco.keypad[0] = 10; // *
                    }
                    break;

                case 0x1796de5e:    // Boulder Dash
                case 0x5933ac18:    // Boulder Dash
                case 0x6e5c4b11:    // Boulder Dash
                    if (joystick.values[ODROID_INPUT_START])
                    {
                        coleco.keypad[0] = 11; // #
                    }

                    if (joystick.values[ODROID_INPUT_START] &&
                        joystick.values[ODROID_INPUT_LEFT])
                    {
                        coleco.keypad[0] = 1;
                    }
                    break;
                case 0x109699e2:    // Dr. Seuss's Fix-Up The Mix-Up Puzzler
                case 0x614bb621:    // Decathlon
                    if (joystick.values[ODROID_INPUT_START])
                    {
                        coleco.keypad[0] = 1;
                    }
                    if (joystick.values[ODROID_INPUT_START] &&
                        joystick.values[ODROID_INPUT_LEFT])
                    {
                        coleco.keypad[0] = 10; // *
                    }
                    break;

                default:
                    if (joystick.values[ODROID_INPUT_START])
                    {
                        coleco.keypad[0] = 1;
                    }
                    break;
            }
        }

        system_frame(skipFrame);

        if (skipFrame)
        {
            ++skippedFrames;
        }
        else
        {
            currentUpdate->width  = bitmap.viewport.w;
            currentUpdate->height = bitmap.viewport.h;
            currentUpdate->buffer = bitmap.data + bitmap.viewport.x;
            currentUpdate->stride = bitmap.pitch;
            render_copy_palette(currentUpdate->palette);

            struct video_update *previousUpdate = (currentUpdate == &update1) ? &update2 : &update1;

            // Diff buffer
            odroid_buffer_diff(currentUpdate->buffer, previousUpdate->buffer,
                               currentUpdate->palette, previousUpdate->palette,
                               currentUpdate->width, currentUpdate->height,
                               currentUpdate->stride, 1, PIXEL_MASK, PAL_SHIFT_MASK,
                               currentUpdate->diff);

            if (currentUpdate->diff[0].width && currentUpdate->diff[0].repeat == currentUpdate->height) {
                ++fullFrames;
            }

            // Send update data to video queue on other core
            xQueueSend(videoTaskQueue, &currentUpdate, portMAX_DELAY);

            // Flip the update struct so we don't start writing into it while
            // the second core is still updating the screen.
            currentUpdate = previousUpdate;

            // Swap buffers
            currentBuffer = 1 - currentBuffer;
            bitmap.data = framebuffers[currentBuffer];
        }

        if (speedupEnabled)
        {
            skipFrame = emulatedFrames % (2 + speedupEnabled);
            snd.enabled = speedupEnabled < 2;
        }
        else
        {
            snd.enabled = true;

            // See if we need to skip a frame to keep up
            skipFrame = (!skipFrame && get_elapsed_time_since(startTime) > frameTime);

            // Process audio
            for (short i = 0; i < snd.sample_count; i++)
            {
                audioBuffer[i] = snd.output[0][i] << 16 | snd.output[1][i];
            }

            odroid_audio_submit((short*)audioBuffer, snd.sample_count);
        }


        totalElapsedTime += get_elapsed_time_since(startTime);
        ++emulatedFrames;

        if (emulatedFrames == refresh)
        {
            float seconds = totalElapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f);
            float fps = emulatedFrames / seconds;

            odroid_battery_state battery;
            odroid_input_battery_level_read(&battery);

            printf("HEAP:%d, FPS:%f, SKIP:%d, FULL:%d, BATTERY:%d [%d]\n",
                esp_get_free_heap_size() / 1024, fps, skippedFrames, fullFrames,
                battery.millivolts, battery.percentage);

            emulatedFrames = 0;
            skippedFrames = 0;
            fullFrames = 0;
            totalElapsedTime = 0;
        }
        previousJoystickState = joystick;
    }
}
