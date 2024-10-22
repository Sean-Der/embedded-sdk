#include <driver/i2s_std.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_system.h>
#include <opus.h>

#include "es8311.h"
#include "main.h"

#define EXAMPLENUM_CHANNELS 2
#define SLOT_MODE I2S_SLOT_MODE_STEREO
#define EXAMPLE_SAMPLE_RATE (8000)
#define SAMPLE_RATE EXAMPLE_SAMPLE_RATE

#define OPUS_ENCODER_BITRATE 30000  // 64000
#define OPUS_ENCODER_COMPLEXITY 0
// Encode: 2 ch, 16-bit, 16Khz, 20 ms = 320 samples/ch = 640 samples = 1280 bits
#define OPUS_ENCODE_BUFFER_SAMPLES_PER_CHANNEL 320
#define OPUS_ENCODE_BUFFER_SIZE                                  \
  OPUS_ENCODE_BUFFER_SAMPLES_PER_CHANNEL *EXAMPLE_NUM_CHANNELS * \
      sizeof(opus_int16)
// Decode: 2 ch, 16-bit, 16Khz, <= 120 ms = 1920 samples/ch = 3840 samples =
// 7680 bits
#define OPUS_DECODE_BUFFER_SAMPLES_PER_CHANNEL 1920
#define OPUS_DECODE_BUFFER_SIZE                                  \
  OPUS_DECODE_BUFFER_SAMPLES_PER_CHANNEL *EXAMPLE_NUM_CHANNELS * \
      sizeof(opus_int16)
#define OPUS_OUT_BUFFER_SIZE 4000  // 4000 bytes is recommended by opus_encode

/* Example configurations */
#define EXAMPLE_MCLK_MULTIPLE i2s_mclk_multiple_t(384)
#define EXAMPLE_MCLK_FREQ_HZ (EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE)
#define EXAMPLE_VOICE_VOLUME 90  // 80 CONFIG_EXAMPLE_VOICE_VOLUME
#define EXAMPLE_MIC_GAIN ES8311_MIC_GAIN_12DB

/* I2C port and GPIOs */
#define I2C_NUM i2c_port_t(0)
#define I2C_SCL_IO (GPIO_NUM_18)
#define I2C_SDA_IO (GPIO_NUM_17)

/* I2S port and GPIOs */
#define I2S_NUM i2s_port_t(0)
#define I2S_MCK_IO (GPIO_NUM_16)
#define I2S_BCK_IO (GPIO_NUM_2)
#define I2S_WS_IO (GPIO_NUM_1)
#define I2S_DO_IO (GPIO_NUM_8)
#define I2S_DI_IO (GPIO_NUM_10)

static const char *TAG = "media";
static const char err_reason[][30] = {"input param is invalid",
                                      "operation timeout"};
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

static esp_err_t es8311_codec_init(void) {
  /* Initialize I2C peripheral */
  const i2c_config_t es_i2c_cfg = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_SDA_IO,
      .scl_io_num = I2C_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master =
          {
              .clk_speed = 100000,
          },
      .clk_flags = 0,
  };
  ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM, &es_i2c_cfg), TAG,
                      "config i2c failed");
  ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0),
                      TAG, "install i2c driver failed");

  /* Initialize es8311 codec */
  es8311_handle_t es_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
  ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");
  const es8311_clock_config_t es_clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = EXAMPLE_MCLK_FREQ_HZ,
      .sample_frequency = EXAMPLE_SAMPLE_RATE};

  ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16,
                              ES8311_RESOLUTION_16));
  ESP_RETURN_ON_ERROR(
      es8311_sample_frequency_config(
          es_handle, EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE,
          EXAMPLE_SAMPLE_RATE),
      TAG, "set es8311 sample frequency failed");
  ESP_RETURN_ON_ERROR(
      es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL), TAG,
      "set es8311 volume failed");
  ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG,
                      "set es8311 microphone failed");
  ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, EXAMPLE_MIC_GAIN),
                      TAG, "set es8311 microphone gain failed");
  return ESP_OK;
}

static esp_err_t i2s_driver_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;  // Auto clear the legacy data in the DMA buffer
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      SLOT_MODE),
      .gpio_cfg =
          {
              .mclk = I2S_MCK_IO,
              .bclk = I2S_BCK_IO,
              .ws = I2S_WS_IO,
              .dout = I2S_DO_IO,
              .din = I2S_DI_IO,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
  return ESP_OK;
}

void lk_init_audio_capture() {
  if (i2s_driver_init() != ESP_OK) {
    ESP_LOGE(TAG, "i2s driver init failed");
    abort();
  } else {
    ESP_LOGI(TAG, "i2s driver init success");
  }
  /* Initialize i2c peripheral and config es8311 codec by i2c */
  if (es8311_codec_init() != ESP_OK) {
    ESP_LOGE(TAG, "es8311 codec init failed");
    abort();
  } else {
    ESP_LOGI(TAG, "es8311 codec init success");
  }
}

opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

void lk_init_audio_decoder() {
  int decoder_error = 0;
  opus_decoder =
      opus_decoder_create(SAMPLE_RATE, EXAMPLE_NUM_CHANNELS, &decoder_error);
  if (decoder_error != OPUS_OK) {
    printf("Failed to create Opus decoder");
    return;
  }

  output_buffer = (opus_int16 *)malloc(OPUS_DECODE_BUFFER_SIZE);
  ESP_LOGI(TAG, "Initialized Opus decoder");
}

void lk_audio_decode(uint8_t *data, size_t size) {
  int decoded_samples = opus_decode(opus_decoder, data, size, output_buffer,
                                    OPUS_DECODE_BUFFER_SAMPLES_PER_CHANNEL, 0);
  if (decoded_samples <= 0) {
    ESP_LOGE(TAG, "Failed to decode audio");
    return;
  }

  size_t bytes_written = 0;
#ifndef CONFIG_EXAMPLE_MODE_ECHO
  size_t bytes_decoded =
      decoded_samples * EXAMPLE_NUM_CHANNELS * sizeof(opus_int16);
  int ret = i2s_channel_write(tx_handle, output_buffer, bytes_decoded,
                              &bytes_written, 1000);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[echo] i2s write failed, %s",
             err_reason[ret == ESP_ERR_TIMEOUT]);
    abort();
  }
  if (bytes_written != bytes_decoded) {
    ESP_LOGW(TAG, "%d bytes read but only %d bytes are written", bytes_decoded,
             bytes_written);
  }
#endif
}

OpusEncoder *opus_encoder = NULL;
opus_int16 *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;

void lk_init_audio_encoder() {
  int encoder_error;
  opus_encoder = opus_encoder_create(SAMPLE_RATE, EXAMPLE_NUM_CHANNELS,
                                     OPUS_APPLICATION_VOIP, &encoder_error);
  if (encoder_error != OPUS_OK) {
    printf("Failed to create OPUS encoder");
    return;
  }

  opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
  opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
  opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  encoder_input_buffer = (opus_int16 *)malloc(OPUS_ENCODE_BUFFER_SIZE);
  encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
}

void lk_send_audio(PeerConnection *peer_connection) {
  size_t bytes_read = 0;
  i2s_channel_read(rx_handle, encoder_input_buffer, OPUS_ENCODE_BUFFER_SIZE,
                   &bytes_read, portMAX_DELAY);

#ifndef CONFIG_EXAMPLE_MODE_ECHO
  auto encoded_size =
      opus_encode(opus_encoder, encoder_input_buffer,
                  bytes_read / (EXAMPLE_NUM_CHANNELS * sizeof(opus_int16)),
                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

  peer_connection_send_audio(peer_connection, encoder_output_buffer,
                             encoded_size);
#else
  i2s_channel_write(tx_handle, encoder_input_buffer, bytes_read, NULL, 1000);
#endif
}
