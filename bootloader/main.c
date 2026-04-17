#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// --- 版本与配置参数 ---
#define BL_VERSION_MAJOR 1U
#define BL_VERSION_MINOR 0U
#define BL_BAUDRATE 115200U

#define APP_BASE_ADDR 0x08004000UL
#define APP_MAX_SIZE (48UL * 1024UL)
#define APP_END_ADDR (APP_BASE_ADDR + APP_MAX_SIZE)

#define SRAM_BASE_ADDR 0x20000000UL
#define SRAM_END_ADDR 0x2000C000UL

#define PKT_SOF 0x7EU
#define ACK_MASK 0x80U
// 修复：将缓冲区扩大，以容纳 128字节数据 + 6字节头部
#define PACKET_MAX_PAYLOAD 256U

#define CMD_PING 0x01U
#define CMD_INFO 0x02U
#define CMD_ERASE 0x03U
#define CMD_WRITE 0x04U
#define CMD_BOOT 0x05U

// --- 硬件定义 ---
#define BOARD_VARIANT 0

#if BOARD_VARIANT == 0
#define BUZZER_PIN GPIO_PIN_4
#define BUZZER_PORT GPIOA
#define BUZZER_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define OFF_PIN   GPIO_PIN_5
#define OFF_PORT  GPIOA
#define OFF_CLK_ENABLE()    __HAL_RCC_GPIOA_CLK_ENABLE()
#elif BOARD_VARIANT == 1
#define BUZZER_PIN GPIO_PIN_13
#define BUZZER_PORT GPIOC
#define BUZZER_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define OFF_PIN   GPIO_PIN_15
#define OFF_PORT  GPIOC
#define OFF_CLK_ENABLE()    __HAL_RCC_GPIOC_CLK_ENABLE()
#endif

static UART_HandleTypeDef huart3;

// --- 函数声明 ---
static void SystemClock_Config(void);
static void UART3_Init(void);
static void GPIO_Init(void);
static void boot_jump_to_app(void);
static bool boot_is_app_valid(void);
static void uart_send(const uint8_t *data, uint16_t len);
static bool uart_recv_byte(uint8_t *byte, uint32_t timeout_ms);
static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);
static bool recv_packet(uint8_t *cmd, uint8_t *payload, uint16_t *payload_len,
                        uint32_t timeout_ms);
static void send_packet(uint8_t cmd, const uint8_t *payload,
                        uint16_t payload_len);
static HAL_StatusTypeDef flash_erase_app(void);
static HAL_StatusTypeDef flash_program(uint32_t address, const uint8_t *data,
                                       uint16_t len);

static void boot_print(const char *msg) {
  uart_send((const uint8_t *)msg, (uint16_t)strlen(msg));
}

// --- 辅助函数 ---
static void delay_us(uint32_t us) {
  volatile uint32_t count = us * 10;
  while (count--) {
    __NOP();
  }
}

static void beep(uint32_t freq, uint32_t duration_ms) {
  uint32_t start = HAL_GetTick();
  uint32_t delay = freq * 62;
  while ((HAL_GetTick() - start) < duration_ms) {
    HAL_GPIO_TogglePin(BUZZER_PORT, BUZZER_PIN);
    delay_us(delay);
  }
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
}

// --- UART 核心逻辑 ---
static void uart_send(const uint8_t *data, uint16_t len) {
  HAL_UART_Transmit(&huart3, (uint8_t *)data, len, 100U);
}

static bool uart_recv_byte(uint8_t *byte, uint32_t timeout_ms) {
  return HAL_UART_Receive(&huart3, byte, 1U, timeout_ms) == HAL_OK;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)data[i] << 8U);
    for (uint8_t bit = 0; bit < 8U; bit++) {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U)
                            : (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

static bool recv_packet(uint8_t *cmd, uint8_t *payload, uint16_t *payload_len,
                        uint32_t timeout_ms) {
  uint8_t byte = 0;
  if (!uart_recv_byte(&byte, timeout_ms) || byte != PKT_SOF)
    return false;

  uint8_t hdr[3];
  if (!uart_recv_byte(&hdr[0], timeout_ms) ||
      !uart_recv_byte(&hdr[1], timeout_ms) ||
      !uart_recv_byte(&hdr[2], timeout_ms))
    return false;

  *cmd = hdr[0];
  *payload_len = (uint16_t)hdr[1] | ((uint16_t)hdr[2] << 8U);

  if (*payload_len > PACKET_MAX_PAYLOAD)
    return false;

  for (uint16_t i = 0; i < *payload_len; i++) {
    if (!uart_recv_byte(&payload[i], timeout_ms))
      return false;
  }

  uint8_t crc_buf[2];
  if (!uart_recv_byte(&crc_buf[0], timeout_ms) ||
      !uart_recv_byte(&crc_buf[1], timeout_ms))
    return false;

  uint16_t rx_crc = (uint16_t)crc_buf[0] | ((uint16_t)crc_buf[1] << 8U);
  uint8_t frame[3 + PACKET_MAX_PAYLOAD];
  frame[0] = hdr[0];
  frame[1] = hdr[1];
  frame[2] = hdr[2];
  memcpy(&frame[3], payload, *payload_len);

  return rx_crc == crc16_ccitt(frame, (uint16_t)(3U + *payload_len));
}

static void send_packet(uint8_t cmd, const uint8_t *payload,
                        uint16_t payload_len) {
  uint8_t hdr[4] = {PKT_SOF, cmd, (uint8_t)(payload_len & 0xFFU),
                    (uint8_t)(payload_len >> 8U)};
  uart_send(hdr, sizeof(hdr));
  if (payload_len > 0U)
    uart_send(payload, payload_len);

  uint8_t frame[3 + PACKET_MAX_PAYLOAD];
  frame[0] = cmd;
  frame[1] = (uint8_t)(payload_len & 0xFFU);
  frame[2] = (uint8_t)(payload_len >> 8U);
  if (payload_len > 0U)
    memcpy(&frame[3], payload, payload_len);
  uint16_t crc = crc16_ccitt(frame, (uint16_t)(3U + payload_len));
  uint8_t crc_out[2] = {(uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8U)};
  uart_send(crc_out, sizeof(crc_out));
}

// --- Flash 核心操作 ---
static HAL_StatusTypeDef flash_erase_app(void) {
  FLASH_EraseInitTypeDef erase = {
      .TypeErase = FLASH_TYPEERASE_PAGES,
      .PageAddress = APP_BASE_ADDR,
      .NbPages = (uint32_t)(APP_MAX_SIZE / FLASH_PAGE_SIZE)};
  uint32_t page_error = 0;
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
  HAL_FLASH_Lock();
  return status;
}

static HAL_StatusTypeDef flash_program(uint32_t address, const uint8_t *data,
                                       uint16_t len) {
  HAL_StatusTypeDef status = HAL_OK;
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

  for (uint16_t i = 0; i < len; i += 2U) {
    uint16_t halfword = (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8U);
    status =
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, halfword);
    if (status != HAL_OK)
      break;
  }
  HAL_FLASH_Lock();
  return status;
}

static bool boot_is_app_valid(void) {
  uint32_t app_sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);
  return (app_sp >= SRAM_BASE_ADDR && app_sp <= SRAM_END_ADDR) &&
         (app_pc >= APP_BASE_ADDR && app_pc < APP_END_ADDR);
}

static void boot_jump_to_app(void) {
  typedef void (*pfn_reset_handler)(void);
  uint32_t app_sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

  /* 1. 关闭 SysTick，防止跳转后触发悬空中断 */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  /* 2. 禁用并清除所有 NVIC 中断 (Cortex-M3: 8个寄存器组) */
  for (uint8_t i = 0; i < 8U; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFU;  // 禁用
    NVIC->ICPR[i] = 0xFFFFFFFFU;  // 清除挂起
  }

  /* 3. 复位 RCC 和外设，让 App 从干净状态初始化 */
  __disable_irq();
  HAL_RCC_DeInit();
  HAL_DeInit();

  /* 4. 设置 App 的中断向量表并跳转 */
  SCB->VTOR = APP_BASE_ADDR;
  __DSB();  // 确保写入完成
  __ISB();  // 刷新指令流水线

  __set_MSP(app_sp);
  __enable_irq();  // App 会自己管理中断使能
  ((pfn_reset_handler)app_pc)();
  while (1)
    ;
}

// --- 主程序 ---
int main(void) {
  HAL_Init();
  SystemClock_Config();
  GPIO_Init();
  
  // 即刻拉高电源维持引脚，防止松开电源键后断电
  HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, GPIO_PIN_SET);
  
  UART3_Init();

  boot_print("\r\n[BL] Bootloader Ready (HSI Mode)\r\n");

  uint8_t cmd = 0;
  uint8_t payload[PACKET_MAX_PAYLOAD];
  uint16_t payload_len = 0;
  bool stay_in_bl = false;

  int sync_retries = 20; // 约 2 秒超时 (20 * 100ms)
  while (sync_retries-- > 0) {
    boot_print("[BL] WAITING_SYNC...\r\n");
    beep(8, 50);
    if (recv_packet(&cmd, payload, &payload_len, 100U)) {
      stay_in_bl = true;
      break;
    }
  }

  if (!stay_in_bl) {
    if (boot_is_app_valid()) {
        boot_print("[BL] No Sync, Jumping to App...\r\n");
        HAL_Delay(100);
        boot_jump_to_app();
    } else {
        boot_print("[BL] No Sync and No Valid App! Staying in BL.\r\n");
    }
  } else {
      beep(8, 500);
      boot_print("[BL] SYNC_OK!\r\n");
  }

  while (1) {
    if (!recv_packet(&cmd, payload, &payload_len, HAL_MAX_DELAY))
      continue;

    if (cmd == CMD_PING) {
      uint8_t rsp[2] = {BL_VERSION_MAJOR, BL_VERSION_MINOR};
      send_packet((uint8_t)(CMD_PING | ACK_MASK), rsp, sizeof(rsp));
    } else if (cmd == CMD_INFO) {
      uint32_t info[2] = {APP_BASE_ADDR, APP_MAX_SIZE};
      send_packet((uint8_t)(CMD_INFO | ACK_MASK), (uint8_t *)info, 8);
    } else if (cmd == CMD_ERASE) {
      uint8_t status = (flash_erase_app() == HAL_OK) ? 0x00U : 0x01U;
      send_packet((uint8_t)(CMD_ERASE | ACK_MASK), &status, 1U);
    } else if (cmd == CMD_WRITE) {
      uint32_t addr;
      memcpy(&addr, &payload[0], 4);
      uint16_t len;
      memcpy(&len, &payload[4], 2);

      // 这里的 payload[6] 是真正的固件数据起始
      HAL_StatusTypeDef status = flash_program(addr, &payload[6], len);
      uint8_t result = (uint8_t)status;

      // 额外检查寄存器状态位，提供更精准报错
      if (status != HAL_OK) {
        if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_WRPERR))
          result = 0x10;
        if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_PGERR))
          result = 0x11;
      }

      send_packet((uint8_t)(CMD_WRITE | ACK_MASK), &result, 1U);
    } else if (cmd == CMD_BOOT) {
      uint8_t status = boot_is_app_valid() ? 0x00U : 0x01U;
      send_packet((uint8_t)(CMD_BOOT | ACK_MASK), &status, 1U);
      if (status == 0x00U) {
        HAL_Delay(100);
        boot_jump_to_app();
      }
    }
  }
}

static void GPIO_Init(void) {
  __HAL_RCC_GPIOB_CLK_ENABLE();
  BUZZER_CLK_ENABLE();
  OFF_CLK_ENABLE();
  
  GPIO_InitTypeDef gpio = {0};
  
  // Power Latch
  gpio.Pin = OFF_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OFF_PORT, &gpio);

  // Buzzer
  gpio.Pin = BUZZER_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_PORT, &gpio);

  // UART3 TX
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  // UART3 RX
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
  HAL_UART_Init(&huart3);
}

static void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

void SysTick_Handler(void) { HAL_IncTick(); }