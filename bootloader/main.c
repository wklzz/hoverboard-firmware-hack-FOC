#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"

#define BL_VERSION_MAJOR           1U
#define BL_VERSION_MINOR           0U

#define BL_BAUDRATE                115200U

#define APP_BASE_ADDR              0x08004000UL
#define APP_MAX_SIZE               (48UL * 1024UL)
#define APP_END_ADDR               (APP_BASE_ADDR + APP_MAX_SIZE)

#define SRAM_BASE_ADDR             0x20000000UL
#define SRAM_END_ADDR              0x2000C000UL

#define BOOT_WAIT_MS               1500U
#define PACKET_MAX_PAYLOAD         128U

#define PKT_SOF                    0x7EU
#define ACK_MASK                   0x80U

#define CMD_PING                   0x01U
#define CMD_INFO                   0x02U
#define CMD_ERASE                  0x03U
#define CMD_WRITE                  0x04U
#define CMD_BOOT                   0x05U

static UART_HandleTypeDef huart3;

static void SystemClock_Config(void);
static void UART3_Init(void);
static void GPIO_Init(void);
static void boot_jump_to_app(void);
static bool boot_is_app_valid(void);
static void uart_send(const uint8_t *data, uint16_t len);
static bool uart_recv_byte(uint8_t *byte, uint32_t timeout_ms);
static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);
static bool recv_packet(uint8_t *cmd, uint8_t *payload, uint16_t *payload_len, uint32_t timeout_ms);
static void send_packet(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
static HAL_StatusTypeDef flash_erase_app(void);
static HAL_StatusTypeDef flash_program(uint32_t address, const uint8_t *data, uint16_t len);

static void boot_print(const char *msg) {
  uart_send((const uint8_t *)msg, (uint16_t)strlen(msg));
}

static void uart_send(const uint8_t *data, uint16_t len) {
  HAL_UART_Transmit(&huart3, (uint8_t *)data, len, HAL_MAX_DELAY);
}

static bool uart_recv_byte(uint8_t *byte, uint32_t timeout_ms) {
  return HAL_UART_Receive(&huart3, byte, 1U, timeout_ms) == HAL_OK;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)data[i] << 8U);
    for (uint8_t bit = 0; bit < 8U; bit++) {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

static bool recv_packet(uint8_t *cmd, uint8_t *payload, uint16_t *payload_len, uint32_t timeout_ms) {
  uint8_t byte = 0;
  if (!uart_recv_byte(&byte, timeout_ms) || byte != PKT_SOF) {
    return false;
  }

  uint8_t hdr[3];
  if (!uart_recv_byte(&hdr[0], timeout_ms) || !uart_recv_byte(&hdr[1], timeout_ms) || !uart_recv_byte(&hdr[2], timeout_ms)) {
    return false;
  }

  *cmd = hdr[0];
  *payload_len = (uint16_t)hdr[1] | ((uint16_t)hdr[2] << 8U);
  if (*payload_len > PACKET_MAX_PAYLOAD) {
    return false;
  }

  for (uint16_t i = 0; i < *payload_len; i++) {
    if (!uart_recv_byte(&payload[i], timeout_ms)) {
      return false;
    }
  }

  uint8_t crc_buf[2];
  if (!uart_recv_byte(&crc_buf[0], timeout_ms) || !uart_recv_byte(&crc_buf[1], timeout_ms)) {
    return false;
  }

  uint16_t rx_crc = (uint16_t)crc_buf[0] | ((uint16_t)crc_buf[1] << 8U);
  uint8_t frame[3 + PACKET_MAX_PAYLOAD];
  frame[0] = hdr[0];
  frame[1] = hdr[1];
  frame[2] = hdr[2];
  memcpy(&frame[3], payload, *payload_len);

  return rx_crc == crc16_ccitt(frame, (uint16_t)(3U + *payload_len));
}

static void send_packet(uint8_t cmd, const uint8_t *payload, uint16_t payload_len) {
  uint8_t hdr[4] = { PKT_SOF, cmd, (uint8_t)(payload_len & 0xFFU), (uint8_t)(payload_len >> 8U) };
  uart_send(hdr, sizeof(hdr));
  if (payload_len > 0U) {
    uart_send(payload, payload_len);
  }

  uint8_t frame[3 + PACKET_MAX_PAYLOAD];
  frame[0] = cmd;
  frame[1] = (uint8_t)(payload_len & 0xFFU);
  frame[2] = (uint8_t)(payload_len >> 8U);
  if (payload_len > 0U) {
    memcpy(&frame[3], payload, payload_len);
  }

  uint16_t crc = crc16_ccitt(frame, (uint16_t)(3U + payload_len));
  uint8_t crc_out[2] = { (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8U) };
  uart_send(crc_out, sizeof(crc_out));
}

static HAL_StatusTypeDef flash_erase_app(void) {
  FLASH_EraseInitTypeDef erase = {
    .TypeErase = FLASH_TYPEERASE_PAGES,
    .PageAddress = APP_BASE_ADDR,
    .NbPages = (uint32_t)(APP_MAX_SIZE / FLASH_PAGE_SIZE)
  };
  uint32_t page_error = 0;

  HAL_FLASH_Unlock();
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
  HAL_FLASH_Lock();
  return status;
}

static HAL_StatusTypeDef flash_program(uint32_t address, const uint8_t *data, uint16_t len) {
  if ((address < APP_BASE_ADDR) || ((address + len) > APP_END_ADDR) || ((len & 0x1U) != 0U)) {
    return HAL_ERROR;
  }

  HAL_FLASH_Unlock();
  for (uint16_t i = 0; i < len; i += 2U) {
    uint16_t halfword = (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8U);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, halfword) != HAL_OK) {
      HAL_FLASH_Lock();
      return HAL_ERROR;
    }
  }
  HAL_FLASH_Lock();
  return HAL_OK;
}

static bool boot_is_app_valid(void) {
  uint32_t app_sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

  bool valid_sp = (app_sp >= SRAM_BASE_ADDR) && (app_sp <= SRAM_END_ADDR);
  bool valid_pc = (app_pc >= APP_BASE_ADDR) && (app_pc < APP_END_ADDR);

  return valid_sp && valid_pc;
}

static void boot_jump_to_app(void) {
  typedef void (*pfn_reset_handler)(void);
  uint32_t app_sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

  __disable_irq();
  for (uint8_t irq = 0; irq < 8U; irq++) {
    NVIC->ICER[irq] = 0xFFFFFFFFU;
    NVIC->ICPR[irq] = 0xFFFFFFFFU;
  }

  HAL_RCC_DeInit();
  HAL_DeInit();

  SCB->VTOR = APP_BASE_ADDR;
  __set_MSP(app_sp);
  ((pfn_reset_handler)app_pc)();

  while (1) {
  }
}

int main(void) {
  HAL_Init();
  SystemClock_Config();
  GPIO_Init();
  UART3_Init();

  boot_print("\r\n[BL] USART3(PB10/PB11) ready\r\n");

  uint32_t start = HAL_GetTick();
  bool stay_in_bl = false;
  uint8_t cmd = 0;
  uint8_t payload[PACKET_MAX_PAYLOAD];
  uint16_t payload_len = 0;

  while ((HAL_GetTick() - start) < BOOT_WAIT_MS) {
    if (recv_packet(&cmd, payload, &payload_len, 50U)) {
      stay_in_bl = true;
      break;
    }
  }

  if (!stay_in_bl && boot_is_app_valid()) {
    boot_print("[BL] jump app\r\n");
    boot_jump_to_app();
  }

  boot_print("[BL] OTA mode\r\n");

  while (1) {
    if (!recv_packet(&cmd, payload, &payload_len, HAL_MAX_DELAY)) {
      continue;
    }

    if (cmd == CMD_PING) {
      uint8_t rsp[2] = { BL_VERSION_MAJOR, BL_VERSION_MINOR };
      send_packet((uint8_t)(CMD_PING | ACK_MASK), rsp, sizeof(rsp));
    } else if (cmd == CMD_INFO) {
      uint8_t rsp[8] = {
        (uint8_t)(APP_BASE_ADDR & 0xFFU), (uint8_t)((APP_BASE_ADDR >> 8U) & 0xFFU),
        (uint8_t)((APP_BASE_ADDR >> 16U) & 0xFFU), (uint8_t)((APP_BASE_ADDR >> 24U) & 0xFFU),
        (uint8_t)(APP_MAX_SIZE & 0xFFU), (uint8_t)((APP_MAX_SIZE >> 8U) & 0xFFU),
        (uint8_t)((APP_MAX_SIZE >> 16U) & 0xFFU), (uint8_t)((APP_MAX_SIZE >> 24U) & 0xFFU)
      };
      send_packet((uint8_t)(CMD_INFO | ACK_MASK), rsp, sizeof(rsp));
    } else if (cmd == CMD_ERASE) {
      uint8_t status = (flash_erase_app() == HAL_OK) ? 0x00U : 0x01U;
      send_packet((uint8_t)(CMD_ERASE | ACK_MASK), &status, 1U);
    } else if (cmd == CMD_WRITE) {
      if (payload_len < 6U) {
        uint8_t status = 0x02U;
        send_packet((uint8_t)(CMD_WRITE | ACK_MASK), &status, 1U);
        continue;
      }

      uint32_t address = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8U) |
                         ((uint32_t)payload[2] << 16U) | ((uint32_t)payload[3] << 24U);
      uint16_t length = (uint16_t)payload[4] | ((uint16_t)payload[5] << 8U);

      if (((uint16_t)(length + 6U) != payload_len) || ((length & 0x1U) != 0U)) {
        uint8_t status = 0x03U;
        send_packet((uint8_t)(CMD_WRITE | ACK_MASK), &status, 1U);
        continue;
      }

      uint8_t status = (flash_program(address, &payload[6], length) == HAL_OK) ? 0x00U : 0x01U;
      send_packet((uint8_t)(CMD_WRITE | ACK_MASK), &status, 1U);
    } else if (cmd == CMD_BOOT) {
      uint8_t status = boot_is_app_valid() ? 0x00U : 0x01U;
      send_packet((uint8_t)(CMD_BOOT | ACK_MASK), &status, 1U);
      if (status == 0x00U) {
        HAL_Delay(20U);
        boot_jump_to_app();
      }
    } else {
      uint8_t status = 0xFFU;
      send_packet((uint8_t)(cmd | ACK_MASK), &status, 1U);
    }
  }
}

static void GPIO_Init(void) {
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_11;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &gpio);
}

static void UART3_Init(void) {
  __HAL_RCC_USART3_CLK_ENABLE();

  huart3.Instance = USART3;
  huart3.Init.BaudRate = BL_BAUDRATE;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  HAL_UART_Init(&huart3);
}

static void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}
