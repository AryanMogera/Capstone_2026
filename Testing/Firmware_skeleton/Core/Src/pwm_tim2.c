// pwm_tim2.c
#include "pwm_tim2.h"
#include "stm32f4xx.h"

#define GPIOBEN     (1U << 1)
#define TIM2EN      (1U << 0)

static uint32_t g_arr = 0;

static void gpio_pwm_init(void) {
    RCC->AHB1ENR |= GPIOBEN;

    /* PB3 Configuration (TIM2_CH2) */
    // Set PB3 to Alternate Function Mode (Binary 10)
    GPIOB->MODER &=~	(1U << 6);      // Bit 6 = 0
    GPIOB->MODER |=  	(1U << 7);      // Bit 7 = 1

    // Set PB3 Alternate Function to AF1 (Binary 0001)
    GPIOB->AFR[0] |=  	(1U << 12);    // Bit 12 = 1
    GPIOB->AFR[0] &=~	(1U << 13);    // Bit 13 = 0
    GPIOB->AFR[0] &=~	(1U << 14);    // Bit 14 = 0
    GPIOB->AFR[0] &=~	(1U << 15);    // Bit 15 = 0

    /* PB10 Configuration (TIM2_CH3) */
    // Set PB10 to Alternate Function Mode (Binary 10)
    GPIOB->MODER &=~	(1U << 20);     // Bit 20 = 0
    GPIOB->MODER |=  	(1U << 21);     // Bit 21 = 1

    // Set PB10 Alternate Function to AF1 (Binary 0001) in High Register
    GPIOB->AFR[1] |=  	(1U << 8);     // Bit 8  = 1
    GPIOB->AFR[1] &=~	(1U << 9);     // Bit 9  = 0
    GPIOB->AFR[1] &=~	(1U << 10);    // Bit 10 = 0
    GPIOB->AFR[1] &=~	(1U << 11);    // Bit 11 = 0
}

void PWM_TIM2_Init(uint32_t tim_clk_hz, uint32_t pwm_hz) {
    gpio_pwm_init();
    RCC->APB1ENR |= TIM2EN;

    g_arr = (tim_clk_hz / pwm_hz) - 1U;
    TIM2->PSC = 0;
    TIM2->ARR = g_arr;

    /* Channel 2 (PB3) - PWM Mode 1 */
    TIM2->CCMR1 &=~	(7U << 12);      // Clear bits 14:12
    TIM2->CCMR1 |=  (1U << 14);      // Bit 14 = 1
    TIM2->CCMR1 |=  (1U << 13);      // Bit 13 = 1
    TIM2->CCMR1 &=~	(1U << 12);      // Bit 12 = 0
    TIM2->CCMR1 |=  (1U << 11);      // OC2PE: Preload Enable

    /* Channel 3 (PB10) - PWM Mode 1 */
    TIM2->CCMR2 &=~	(7U << 4);       // Clear bits 6:4
    TIM2->CCMR2 |=  (1U << 6);       // Bit 6 = 1
    TIM2->CCMR2 |=  (1U << 5);       // Bit 5 = 1
    TIM2->CCMR2 &=~	(1U << 4);       // Bit 4 = 0
    TIM2->CCMR2 |=  (1U << 3);       // OC3PE: Preload Enable

    // Enable Channel 2 and 3 Outputs
    TIM2->CCER |= (1U << 4);         // CC2E
    TIM2->CCER |= (1U << 8);         // CC3E

    // Enable ARR Preload and Start Timer
    TIM2->CR1 |= (1U << 7);          // ARPE: Auto-reload preload enable
    TIM2->EGR |= (1U << 0);          // UG: Update Generation (loads settings)
    TIM2->CR1 |= (1U << 0);          // CEN: Counter Enable
}

static uint32_t duty_to_ccr(uint8_t duty) {
    if (duty > 100) duty = 100;
    return ((uint32_t)duty * (g_arr + 1U)) / 100U;
}

	//set Channel 2 (PB3)
void PWM_TIM2_SetDuty_CH2(uint8_t duty_percent) {
    TIM2->CCR2 = duty_to_ccr(duty_percent);
}

	//set Channel 3 (PB10)
void PWM_TIM2_SetDuty_CH3(uint8_t duty_percent) {
    TIM2->CCR3 = duty_to_ccr(duty_percent);
}

	//sets both duty cycles to 0 immediately.
void PWM_TIM2_AllOff(void) {
    TIM2->CCR2 = 0;
    TIM2->CCR3 = 0;
}





//// pwm_tim2.c
//
//#include "pwm_tim2.h"
//#include "stm32f4xx.h"
//
//
//
//#define GPIOBEN		(1U<<1)
//#define TIM2EN		(1U<<0)
//
//
//static uint32_t g_arr = 0;
//
//static void gpio_pwm_init(void) {
//  RCC->AHB1ENR |= GPIOBEN;
//
//  // PB3 = TIM2_CH2, PB10 = TIM2_CH3
//
//  /* PB# Configuration (TIM2_CH2) */
//
//  // Set PB3 to Alternate Function Mode (Binary 10)
//  GPIOB->MODER &=~ (1U << 6);  		// Bit 6 = 0
//  GPIOB->MODER |=  (1U << 7);  		// Bit 7 = 1
//
//
//  // Set PB3 Alternate Function to AF1 (Binary 0001)
//  GPIOB->AFR[0] |=  (1U << 12); 	// Bit 12 = 1
//  GPIOB->AFR[0] &=~	(1U << 13); 	// Bit 13 = 0
//  GPIOB->AFR[0] &=~	(1U << 14); 	// Bit 14 = 0
//  GPIOB->AFR[0] &=~	(1U << 15); 	// Bit 15 = 0
//
//
//  /*  PB10 Configuration (TIM2_CH3)  */
//
//  // Set PB10 to Alternate Function Mode (Binary 10)
//  GPIOB->MODER &=~	(1U << 20); 	// Bit 20 = 0
//  GPIOB->MODER |=  	(1U << 21); 	// Bit 21 = 1
//
//  // Set PB10 Alternate Function to AF1 (Binary 0001)
//  // PB10 is in AFR[1] (High Register).
//  // Calculation: (10-8) * 4 = 8. So it starts at Bit 8.
//
//  GPIOB->AFR[1] |=  (1U << 8);  	// Bit 8  = 1
//  GPIOB->AFR[1] &=~	(1U << 9);  	// Bit 9  = 0
//  GPIOB->AFR[1] &=~	(1U << 10);		// Bit 10 = 0
//  GPIOB->AFR[1] &=~	(1U << 11);		// Bit 11 = 0
//
//}
//
//void PWM_TIM2_Init(uint32_t tim_clk_hz, uint32_t pwm_hz) {
//
//  gpio_pwm_init();
//  RCC->APB1ENR |= TIM2EN;			//Enables the clock for the TIM2 peripheral
//
//  //Frequency Setup
//  g_arr = (tim_clk_hz / pwm_hz) - 1U;	//Calculates the Auto-Reload Value. This number determines the PWM frequency
//
//  TIM2->PSC = 0;		//The Prescaler is set to 0, meaning the timer runs at the full clock speed
//  TIM2->ARR = g_arr;	//The timer will count from 0 up to this value and then reset.
//
//
//  /* Channel 2 Configuration (PB3)  */
//
//  // CCMR1 bits for Channel 2 start at Bit 8
//  // Set PWM Mode 1 (110 binary = 6)
//  TIM2->CCMR1 &= ~(7U << 12); 		// Clear bits 14:12 (OC2M)
//  TIM2->CCMR1 |=  (1U << 14); 		// Bit 14 = 1
//  TIM2->CCMR1 |=  (1U << 13); 		// Bit 13 = 1
//  TIM2->CCMR1 &= ~(1U << 12); 		// Bit 12 = 0
//
//  // Preload Enable (Bit 11)
//  TIM2->CCMR1 |=  (1U << 11);
//
//  /* Channel 3 Configuration (PB10) */
//
//  // CCMR2 handles Channel 3. Starts at Bit 0.
//  // Set PWM Mode 1 (110 binary = 6)
//  TIM2->CCMR2 &= ~(7U << 4);  		// Clear bits 6:4 (OC3M)
//  TIM2->CCMR2 |=  (1U << 6);  		// Bit 6 = 1
//  TIM2->CCMR2 |=  (1U << 5);  		// Bit 5 = 1
//  TIM2->CCMR2 &= ~(1U << 4);  		// Bit 4 = 0
//
//  // Preload Enable (Bit 3)
//  TIM2->CCMR2 |=  (1U << 3);
//
//  // 3. Enable Output bits in CCER
//  TIM2->CCER |= (1U << 4); 			// CC2E: Channel 2 Enable
//  TIM2->CCER |= (1U << 8); 			// CC3E: Channel 3 Enable
//
//  // 4. Force Update and Enable Timer
//  TIM2->EGR |= (1U << 0);  			// UG: Update Generation
//  TIM2->CR1 |= (1U << 0);  			// CEN: Counter Enable
//
//
//
//static uint32_t duty_to_ccr(uint8_t duty) {
//  if (duty > 100) duty = 100;						//if accidentally pass 110 to the function, it forces the value to 100 to prevent math overflow
//  return ((uint32_t)duty * (g_arr + 1U)) / 100U;	//
//}
//
//void PWM_TIM2_SetDuty_CH2(uint8_t duty_percent)
//{
//	TIM2->CCR2 = duty_to_ccr(duty_percent);
//}
//
//void PWM_TIM2_SetDuty_CH3(uint8_t duty_percent) {
//
//	TIM2->CCR3 = duty_to_ccr(duty_percent);
//}
//
////sets both duty cycles to 0 immediately.
//void PWM_TIM2_AllOff(void) {
//  TIM2->CCR2 = 0;
//  TIM2->CCR3 = 0;
//}
//
//
//
////// CH2 PWM1 + preload
////TIM2->CCMR1 &=~	(TIM_CCMR1_OC2M | TIM_CCMR1_CC2S);
////TIM2->CCMR1 |=  	(6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
////
////// CH3 PWM1 + preload
////TIM2->CCMR2 &=~	(TIM_CCMR2_OC3M | TIM_CCMR2_CC3S);
////TIM2->CCMR2 |=  	(6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE;
////
////TIM2->CCER  |= TIM_CCER_CC2E | TIM_CCER_CC3E;
////
////TIM2->CCR2 = 0;
////TIM2->CCR3 = 0;
////
////TIM2->CR1 |= TIM_CR1_ARPE;
////TIM2->EGR  = TIM_EGR_UG;
////TIM2->CR1 |= TIM_CR1_CEN;
////}
