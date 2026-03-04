// bms_hw.c  (Passive balancing + CHG/DSG enables, active-high + USER button)
// Pins used:
//  - Bleed: PB4..PB7  (cell0..cell3)
//  - CHG_EN: PC6
//  - DSG_EN: PC7
//  - USER button (Nucleo B1): PC13

#include "bms_hw.h"
#include "stm32f4xx.h"

#define GPIOBEN (1U << 1)
#define GPIOCEN (1U << 2)

// Bleed pins (PB4..PB7)
#define BAL1_PIN 4U
#define BAL2_PIN 5U
#define BAL3_PIN 6U
#define BAL4_PIN 7U

// Charge / Discharge enable pins (PC6/PC7)
#define CHG_PIN  6U
#define DSG_PIN  7U

// Nucleo USER button (B1) pin
#define USER_BTN_PIN 13U   // PC13

static inline void gpio_pin_output(GPIO_TypeDef *GPIOx, uint32_t pin) {
  GPIOx->MODER &= ~(3U << (pin * 2U));
  GPIOx->MODER |=  (1U << (pin * 2U));     // output

  GPIOx->OTYPER &= ~(1U << pin);           // push-pull
  GPIOx->PUPDR  &= ~(3U << (pin * 2U));    // no pull

  GPIOx->OSPEEDR &= ~(3U << (pin * 2U));
  GPIOx->OSPEEDR |=  (1U << (pin * 2U));   // medium speed
}

static inline void gpio_pin_input(GPIO_TypeDef *GPIOx, uint32_t pin) {
  // input mode
  GPIOx->MODER &= ~(3U << (pin * 2U));

  // For Nucleo B1 on PC13: board usually already has pull-up/down.
  // Leave floating (no internal pull) to avoid fighting the board circuit.
  GPIOx->PUPDR &= ~(3U << (pin * 2U));
}

static inline void pin_write(GPIO_TypeDef *GPIOx, uint32_t pin, bool on) {
  if (on) GPIOx->BSRR = (1U << pin);
  else    GPIOx->BSRR = (1U << (pin + 16U));
}

void BMS_HW_Init(void) {
  // Enable GPIO clocks for ports we use
  RCC->AHB1ENR |= (GPIOBEN | GPIOCEN);

  // Configure bleed pins as outputs
  gpio_pin_output(GPIOB, BAL1_PIN);
  gpio_pin_output(GPIOB, BAL2_PIN);
  gpio_pin_output(GPIOB, BAL3_PIN);
  gpio_pin_output(GPIOB, BAL4_PIN);

  // Configure charge/discharge enable pins as outputs
  gpio_pin_output(GPIOC, CHG_PIN);
  gpio_pin_output(GPIOC, DSG_PIN);

  // Configure USER button as input
  gpio_pin_input(GPIOC, USER_BTN_PIN);

  // Safe defaults
  BMS_HW_SetChargeEnable(false);
  BMS_HW_SetDischargeEnable(false);
  BMS_HW_Bleed_AllOff();
}

void BMS_HW_SetChargeEnable(bool on) {
  pin_write(GPIOC, CHG_PIN, on); // Active-high enable
}

void BMS_HW_SetDischargeEnable(bool on) {
  pin_write(GPIOC, DSG_PIN, on); // Active-high enable
}

void BMS_HW_Bleed_AllOff(void) {
  pin_write(GPIOB, BAL1_PIN, false);
  pin_write(GPIOB, BAL2_PIN, false);
  pin_write(GPIOB, BAL3_PIN, false);
  pin_write(GPIOB, BAL4_PIN, false);
}

void BMS_HW_Bleed_Set(uint8_t cell, bool on) {
  switch (cell) {
    case 0: pin_write(GPIOB, BAL1_PIN, on); break;
    case 1: pin_write(GPIOB, BAL2_PIN, on); break;
    case 2: pin_write(GPIOB, BAL3_PIN, on); break;
    case 3: pin_write(GPIOB, BAL4_PIN, on); break;
    default: break;
  }
}

// PC13 on many Nucleo boards is ACTIVE-LOW
//   pressed  -> IDR bit = 0
//   released -> IDR bit = 1
bool BMS_HW_UserButtonPressed(void) {
  return (GPIOC->IDR & (1U << USER_BTN_PIN)) ? false : true;
}


