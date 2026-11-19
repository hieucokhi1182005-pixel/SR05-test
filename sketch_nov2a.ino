#include <driver/i2s.h>

// --- CHÂN MIC INMP441 (I2S_NUM_0) ---
#define MIC_WS   4
#define MIC_SCK  5
#define MIC_SD   6
#define I2S_MIC_PORT I2S_NUM_0

// --- CHÂN LOA MAX98357A (I2S_NUM_1) ---
#define SPK_LRC  16
#define SPK_BCLK 15
#define SPK_DIN  7
#define I2S_SPK_PORT I2S_NUM_1

#define BUTTON_PIN 0

#define SAMPLE_RATE     16000
#define RECORD_SECONDS  5
#define TOTAL_SAMPLES   (SAMPLE_RATE * RECORD_SECONDS)
#define GAIN_FACTOR     1.2f   // Giảm gain về 1.2 để tránh vỡ tiếng (clipping)

int16_t* audioBuffer = NULL;

void setupMic() {
  const i2s_config_t i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  const i2s_pin_config_t mic_pin_config = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &mic_pin_config);
  i2s_zero_dma_buffer(I2S_MIC_PORT);
}

void setupSpeaker() {
  const i2s_config_t i2s_spk_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    // Dùng chuẩn RIGHT_LEFT (Stereo) để MAX98357A nhận đúng khung xung chuẩn, hết rè
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  const i2s_pin_config_t spk_pin_config = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPK_PORT, &i2s_spk_config, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &spk_pin_config);
  i2s_zero_dma_buffer(I2S_SPK_PORT);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(1000);

  audioBuffer = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
  if (audioBuffer == NULL) {
    Serial.println("Loi: Khong du RAM!");
    while (true);
  }

  setupMic();
  setupSpeaker();

  Serial.println("\n=== SAN SANG (DA FIX RE) ===");
  Serial.println("Nhan nut BOOT (GPIO 0) hoac gui ky tu qua Serial de ghi am 5 giay...");
}

void recordAndPlay() {
  Serial.println("\n[DANG GHI AM 5 GIAY...]");
  i2s_zero_dma_buffer(I2S_MIC_PORT);

  int32_t rawBlock[256];
  size_t bytesRead = 0;
  int samplesCollected = 0;

  // Biến cho bộ lọc thông cao (DC Blocker) loại bỏ tiếng ù/rè tần số thấp
  float prevIn = 0.0f;
  float prevOut = 0.0f;
  const float alpha = 0.995f;

  while (samplesCollected < TOTAL_SAMPLES) {
    i2s_read(I2S_MIC_PORT, rawBlock, sizeof(rawBlock), &bytesRead, portMAX_DELAY);
    int samplesInBlock = bytesRead / sizeof(int32_t);

    for (int i = 0; i < samplesInBlock && samplesCollected < TOTAL_SAMPLES; i++) {
      // Lấy 16-bit sạch từ dữ liệu 32-bit
      float rawSample = (float)(rawBlock[i] >> 16);

      // Lọc bỏ DC Offset (High-pass filter)
      float filtered = alpha * (prevOut + rawSample - prevIn);
      prevIn = rawSample;
      prevOut = filtered;

      // Nhân gain và chống vỡ tiếng
      int32_t val = (int32_t)(filtered * GAIN_FACTOR);
      if (val > 32767) val = 32767;
      if (val < -32768) val = -32768;

      audioBuffer[samplesCollected++] = (int16_t)val;
    }
  }
  Serial.println("[GHI AM XONG!]");
  delay(200);

  Serial.println("[DANG PHAT LAI RA LOA...]");
  
  // Chuyển đổi từng khối nhỏ từ Mono sang Stereo (L = R) trước khi đẩy ra MAX98357A
  int16_t stereoBlock[512]; // 256 mẫu L + 256 mẫu R
  size_t bytesWritten = 0;
  int idx = 0;

  while (idx < TOTAL_SAMPLES) {
    int count = 0;
    for (int i = 0; i < 256 && idx < TOTAL_SAMPLES; i++) {
      int16_t sample = audioBuffer[idx++];
      stereoBlock[count++] = sample; // Kênh trái (L)
      stereoBlock[count++] = sample; // Kênh phải (R)
    }
    i2s_write(I2S_SPK_PORT, stereoBlock, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  }

  i2s_zero_dma_buffer(I2S_SPK_PORT);
  Serial.println("[HOAN TAT!]");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW || Serial.available() > 0) {
    while (Serial.available() > 0) Serial.read();
    recordAndPlay();
    delay(500);
  }
}