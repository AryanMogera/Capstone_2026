#include "stm32f446xx.h"
// bm_practice_f446: bare-metal LED blink test (PA5)


/* Nucleo-F446RE onboard LED = PA5 */
#define GPIOAEN   (1U << 0)
#define LED_PIN  (1U << 5)

void delay(volatile uint32_t t)
{
    while (t--) { }
}

int main(void)
{
    /* Enable clock for GPIOA */
    RCC->AHB1ENR |= GPIOAEN;

    /* Set PA5 as output */
    GPIOA->MODER &= ~(1U << 11);   // clear bit
    GPIOA->MODER |=  (1U << 10);   // set bit → output mode

    while (1)
    {
        /* Toggle LED */
        GPIOA->ODR ^= LED_PIN;

        /* Delay */
        delay(800000);
    }
}

