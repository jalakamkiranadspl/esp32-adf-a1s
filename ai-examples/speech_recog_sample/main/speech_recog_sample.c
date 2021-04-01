/* 
This is a sample program for the esp32 speech recognition kit

*/

//Include libraries and header files



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "board.h"
#include "audio_common.h"
#include "audio_pipeline.h"
#include "mp3_decoder.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "/home/jalakamkiran/esp/esp32-adf-a1s-master/components/esp-adf-libs/audio_misc/include/rec_eng_helper.h"




//Constant tag definitions
static const char *TAG = "Speech Recognition Sample";
static const char *EVENT_TAG = "Speech recognition Event";


typedef enum {
    WAKE_UP = 1,
    OPEN_THE_LIGHT,
    CLOSE_THE_LIGHT,
    VOLUME_INCREASE,
    VOLUME_DOWN,
    PLAY,
    PAUSE,
    MUTE,
    PLAY_LOCAL_MUSIC
} asr_event_t;



void app_main(void){
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Print generanl system info
    printf("\n -----Getting System info-------\n");
    printf("ESP-IDF sdk version running is : %s \n",esp_get_idf_version());

    #if defined CONFIG_ESP_LYRAT_V4_3_BOARD
        gpio_config_t gpio_conf = {
            .pin_bit_mask = 1UL << get_green_led_gpio(),
            .mode = GPIO_MODE_OUTPUT(),
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = 0
        };
        gpio_config(&gpio_conf);
    #endif
        esp_log_level_set("*",ESP_LOG_WARN);
        esp_log_level_set(TAG,ESP_LOG_INFO);
        esp_log_level_set(EVENT_TAG,ESP_LOG_INFO);

        ESP_LOGI(TAG,"Initializing SR handle");
        esp_wn_iface_t *wakenet;
        model_coeff_getter_t *model_coeff_getter;
        model_iface_data_t *model_data;

        //Initialize wakenet model data
        get_wakenet_iface(&wakenet);
        get_wakenet_coeff(&model_coeff_getter);
        model_data = wakenet->create(model_coeff_getter,DET_MODE_90);

        int num = wakenet->get_word_num(model_data);
       //To print all the wake words than can be used to trigger the bot
        for (int i = 1; i <= num; i++)
        {
            char *name = wakenet->get_word_name(model_data,i);
            ESP_LOGI(TAG,"Key words : %s (index = %d)",name,i);
        }
        
        float threshold = wakenet->get_det_threshold(model_data,1);
        int audio_chunksize = wakenet->get_samp_chunksize(model_data);
        int frequency = wakenet->get_samp_rate(model_data);

        ESP_LOGI(EVENT_TAG,"Total keywords = %d, Threshold = %f, Frequency = %d, chunksize =%d, sizeof_uint16 = %d",num,threshold,frequency,audio_chunksize,sizeof(int16_t));


        int16_t *buffer = (int16_t *) malloc(audio_chunksize * sizeof(int16_t));

        if(NULL == buffer)
        {
            ESP_LOGE(EVENT_TAG,"Unable to allocate memory");
            wakenet->destroy(model_data);
            model_data = NULL;
            return;
        }
        
        ESP_LOGI(EVENT_TAG, "Start codec chip");
        audio_board_handle_t board_handle = audio_board_init();
        audio_hal_ctrl_codec(board_handle->audio_hal,AUDIO_HAL_CODEC_MODE_BOTH,AUDIO_HAL_CTRL_START);

        audio_pipeline_handle_t pipline;
        audio_element_handle_t i2s_stream_reader,filter,raw_read;

        ESP_LOGI(EVENT_TAG," Creating a audio pipeline for recordings");
        audio_pipeline_cfg_t pipline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
        pipline = audio_pipeline_init(&pipline_cfg);
        mem_assert(pipline);

        ESP_LOGI(EVENT_TAG,"Creating i2s stream to read the dataa");
        i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        i2s_cfg.i2s_config.sample_rate = 48000;
        i2s_cfg.type = AUDIO_STREAM_READER;

    #if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
        i2s_cfg.i2s_config.sample_rate = 16000;
        i2s_cfg.i2s_port = 1;
        i2s_cfg.i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
        i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    #else 
        i2s_stream_reader = i2s_stream_init(&i2s_cfg);
        ESP_LOGI(EVENT_TAG,"Creating multiple filter to resample the data");
        rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
        rsp_cfg.src_rate = 48000;
        rsp_cfg.src_ch = 2;
        rsp_cfg.dest_rate = 16000;
        rsp_cfg.dest_ch =1;
        filter = rsp_filter_init(&rsp_cfg);
    #endif

    ESP_LOGI(EVENT_TAG,"Creating raw to recieve data in raw format");
    raw_stream_cfg_t raw_cfg = {
        .out_rb_size = 8*1024,
        .type = AUDIO_STREAM_READER
    };
    raw_read = raw_stream_init(&raw_cfg);


    ESP_LOGI(EVENT_TAG,"Registering pipelines");
    audio_pipeline_register(pipline,i2s_stream_reader,"i2s");
    audio_pipeline_register(pipline,raw_read,"raw");

    while (1)
    {
        raw_stream_read(raw_read,(char *)buffer,audio_chunksize);
        int keyword = wakenet->detect(model_data,(int16_t *)buffer);
        switch (keyword)
        {
        case WAKE_UP:
            ESP_LOGI(TAG,"WAKE UP");
            break;
        case OPEN_THE_LIGHT:
            ESP_LOGI(TAG, "Turn on the light");
        #if defined CONFIG_ESP_LYRAT_V4_3_BOARD
                gpio_set_level(get_green_led_gpio(), 1);
        #endif
                break;
        case CLOSE_THE_LIGHT:
            ESP_LOGI(TAG, "Turn off the light");
        #if defined CONFIG_ESP_LYRAT_V4_3_BOARD
            gpio_set_level(get_green_led_gpio(), 0);
        #endif
        break;
        case VOLUME_INCREASE:
            ESP_LOGI(TAG, "volume increase");
            break;
        case VOLUME_DOWN:
            ESP_LOGI(TAG, "volume down");
            break;
        case PLAY:
            ESP_LOGI(TAG, "play");
            break;
        case PAUSE:
            ESP_LOGI(TAG, "pause");
            break;
        case MUTE:
            ESP_LOGI(TAG, "mute");
            break;
        case PLAY_LOCAL_MUSIC:
            ESP_LOGI(TAG, "play local music");
            break;
        default:
            ESP_LOGD(TAG, "Not supported keyword");
            break;
        }
    }

    ESP_LOGI(EVENT_TAG,"Stoping audio pipeline");

    audio_pipeline_terminate(pipline);
    audio_pipeline_remove_listener(pipline);

    audio_pipeline_unregister(pipline,raw_read);
    audio_pipeline_unregister(pipline,i2s_stream_reader);
    audio_pipeline_unregister(pipline,filter);

    audio_pipeline_deinit(pipline);
    audio_element_deinit(raw_read);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(filter);

    ESP_LOGI(EVENT_TAG,"Destroying model");

    wakenet->destroy(model_data);
    model_data = NULL;
    free(buffer);
    buffer = NULL;
}