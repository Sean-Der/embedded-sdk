#include <driver/i2s.h>
#include <driver/i2s_pdm.h>
#include <driver/i2s_std.h>
#include <esp_camera.h>
#include <esp_h264_alloc.h>
#include <esp_h264_enc_single.h>
#include <esp_h264_enc_single_sw.h>
#include <opus.h>

#include "main.h"

#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode

#define MCLK_PIN 0
#define DAC_BCLK_PIN 15
#define DAC_LRCLK_PIN 16
#define DAC_DATA_PIN 17
#define ADC_BCLK_PIN 38
#define ADC_LRCLK_PIN 39
#define ADC_DATA_PIN 40

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0

#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

void *esp_h264_aligned_calloc(uint32_t alignment, uint32_t n, uint32_t size,
                              uint32_t *actual_size, uint32_t caps) {
  *actual_size = ALIGN_UP(n * size, alignment);
  void *out_ptr = heap_caps_aligned_calloc((size_t)alignment, (size_t)n,
                                           (size_t)size, caps);
  return out_ptr;
}

/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <unistd.h>

#include "sdkconfig.h"

#ifdef CONFIG_FREERTOS_UNICORE
#define CPU_NUM 1
#else
#define CPU_NUM CONFIG_SOC_CPU_CORES_NUM
#endif

long sysconf(int arg) {
  switch (arg) {
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
      return CPU_NUM;
    default:
      errno = EINVAL;
      return -1;
  }
}

void lk_init_audio_capture() {
  i2s_config_t i2s_config_out = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
      .tx_desc_auto_clear = true,
  };
  if (i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio output");
    return;
  }

  i2s_pin_config_t pin_config_out = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = DAC_BCLK_PIN,
      .ws_io_num = DAC_LRCLK_PIN,
      .data_out_num = DAC_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_set_pin(I2S_NUM_0, &pin_config_out) != ESP_OK) {
    printf("Failed to set I2S pins for audio output");
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);

  i2s_config_t i2s_config_in = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
  };
  if (i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio input");
    return;
  }

  i2s_pin_config_t pin_config_in = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = ADC_BCLK_PIN,
      .ws_io_num = ADC_LRCLK_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = ADC_DATA_PIN,
  };
  if (i2s_set_pin(I2S_NUM_1, &pin_config_in) != ESP_OK) {
    printf("Failed to set I2S pins for audio input");
    return;
  }
}

opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

void lk_init_audio_decoder() {
  int decoder_error = 0;
  opus_decoder = opus_decoder_create(SAMPLE_RATE, 2, &decoder_error);
  if (decoder_error != OPUS_OK) {
    printf("Failed to create OPUS decoder");
    return;
  }

  output_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
}

void lk_audio_decode(uint8_t *data, size_t size) {
  int decoded_size =
      opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES, 0);

  if (decoded_size > 0) {
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, output_buffer, BUFFER_SAMPLES * sizeof(opus_int16),
              &bytes_written, portMAX_DELAY);
  }
}

OpusEncoder *opus_encoder = NULL;
opus_int16 *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;
esp_h264_enc_handle_t h264_encoder = NULL;

void lk_init_audio_encoder() {
  int encoder_error;
  opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                     &encoder_error);
  if (encoder_error != OPUS_OK) {
    printf("Failed to create OPUS encoder");
    return;
  }

  if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) !=
      OPUS_OK) {
    printf("Failed to initialize OPUS encoder");
    return;
  }

  opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
  opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
  opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  encoder_input_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES);
  encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
}

void lk_send_audio(PeerConnection *peer_connection) {
  size_t bytes_read = 0;

  i2s_read(I2S_NUM_1, encoder_input_buffer, BUFFER_SAMPLES, &bytes_read,
           portMAX_DELAY);

  auto encoded_size =
      opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES / 2,
                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

  peer_connection_send_audio(peer_connection, encoder_output_buffer,
                             encoded_size);
}

camera_fb_t *fb = NULL;
esp_h264_enc_in_frame_t in_frame;
int ret;
void lk_send_video(PeerConnection *peer_connection) {
  fb = esp_camera_fb_get();

  if (!fb) {
    printf("Camera capture failed\n");
    esp_camera_fb_return(fb);
    return;
  }

  esp_h264_enc_out_frame_t out_frame;

  in_frame.raw_data.len = fb->len;
  in_frame.raw_data.buffer = fb->buf;

  out_frame.raw_data.len = fb->width * fb->height * 2;
  out_frame.raw_data.buffer = (uint8_t *)esp_h264_aligned_calloc(
      16, 1, out_frame.raw_data.len, &out_frame.raw_data.len,
      MALLOC_CAP_SPIRAM);

  if ((ret = esp_h264_enc_process(h264_encoder, &in_frame, &out_frame)) !=
      ESP_H264_ERR_OK) {
    printf("failed to encode %d\n", ret);
    heap_caps_free(out_frame.raw_data.buffer);
    esp_camera_fb_return(fb);
    return;
  }

  if ((ret = peer_connection_send_video(peer_connection,
                                        (uint8_t *)out_frame.raw_data.buffer,
                                        (int)out_frame.length)) < 1) {
    printf("failed to send video %d\n", ret);
  }
  heap_caps_free(out_frame.raw_data.buffer);
  esp_camera_fb_return(fb);
}

int lk_init_video_encoder() {
  esp_h264_enc_cfg_sw_t cfg;
  cfg.gop = 20;
  cfg.fps = 20;
  cfg.res.width = 96;
  cfg.res.height = 96;
  cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
  cfg.rc.qp_min = 10;
  cfg.rc.qp_max = 10;
  cfg.pic_type = ESP_H264_RAW_FMT_YUYV;

  int ret;

  if ((ret = esp_h264_enc_sw_new(&cfg, &h264_encoder)) != ESP_H264_ERR_OK) {
    return ret;
  }

  if ((ret = esp_h264_enc_open(h264_encoder)) != ESP_H264_ERR_OK) {
    return ret;
  }

  return ESP_H264_ERR_OK;
}

int lk_init_video_capture() {
  static camera_config_t camera_config = {
      .pin_pwdn = CAM_PIN_PWDN,
      .pin_reset = CAM_PIN_RESET,

      .pin_xclk = CAM_PIN_XCLK,

      .pin_sccb_sda = CAM_PIN_SIOD,
      .pin_sccb_scl = CAM_PIN_SIOC,

      .pin_d7 = CAM_PIN_D7,
      .pin_d6 = CAM_PIN_D6,
      .pin_d5 = CAM_PIN_D5,
      .pin_d4 = CAM_PIN_D4,
      .pin_d3 = CAM_PIN_D3,
      .pin_d2 = CAM_PIN_D2,
      .pin_d1 = CAM_PIN_D1,
      .pin_d0 = CAM_PIN_D0,

      .pin_vsync = CAM_PIN_VSYNC,
      .pin_href = CAM_PIN_HREF,

      .pin_pclk = CAM_PIN_PCLK,

      .xclk_freq_hz = 16000000,

      .ledc_timer = LEDC_TIMER_0,
      .ledc_channel = LEDC_CHANNEL_0,

      .pixel_format = PIXFORMAT_YUV422,  // PIXFORMAT_YUV422,
      .frame_size = FRAMESIZE_96X96,
      .jpeg_quality = 10,
      .fb_count = 2,
      .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

  return esp_camera_init(&camera_config);
}