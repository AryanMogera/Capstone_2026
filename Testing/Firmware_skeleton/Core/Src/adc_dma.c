// adc_dma.c
#include "adc_dma.h"
#include "stm32f4xx.h"

#define VREF   3.3f
#define ADCMAX 4095.0f

#define GPIOAEN		(1U<<0)
#define GPIOBEN		(1U<<1)
#define GPIOCEN		(1U<<2)

/*Clock Enable bits */
#define	DMA2EN		(1U<<22)
#define ADC1EN		(1U<<8)

/* DMA Control Registers Bits */
#define DMA_EN			(1U<<0)
#define DMA_CIRC		(1U<<8)			//Enable circular mode so it doesn't stop after reading 13 values
#define DMA_MINC		(1U<<10)		//tells the DMA to move to the next slot in our array after every piece of data
#define DMA_PSIZE_16	(1U<<11)		//Tells the DMA that each "package" is 16 bits (a half-word)
#define DMA_MSIZE_16	(1U<<13)		//Tells the DMA that each "package" is 16 bits (a half-word)
#define DMA_PL_MED		(2U<<16)		//medium priority (01)


/* ADC Control Register Bits */
#define ADC_CR_2_ADON      	(1U << 0)
#define ADC_CR2_CONT      	(1U << 1)
#define ADC_CR2_DMA       	(1U << 8)
#define ADC_CR2_DDS       	(1U << 9)
#define ADC__CR2_SWSTART   	(1U << 30)
#define ADC_CR1_SCAN      	(1U << 8) // CR1N

static adc_frame_t g_adc;

// ADC channel order (DMA buffer indexes)
// Voltage 1-4, Temperature 1-8, Current 1

static const uint8_t g_seq[ADC_CH_COUNT] = {
  0,   // V1: PA0  ADC123_IN0
  1,   // V2: PA1  ADC123_IN1
  4,   // V3: PA4  ADC12_IN4
  8,   // V4: PB0  ADC12_IN8
  11,  // T1: PC1  ADC123_IN11
  10,  // T2: PC0  ADC123_IN10
  12,  // T3: PC2  ADC123_IN12
  13,  // T4: PC3  ADC123_IN13
  14,  // T5: PC4  ADC12_IN14
  15,  // T6: PC5  ADC12_IN15
  9,   // T7: PB1  ADC12_IN9
  6,   // T8: PA6  ADC12_IN6
  5    // I1: PA5  ADC12_IN5
};

static void gpio_analog_init(void) {
	//Enable clock to GPIOA, GPIOB, and GPIOC
  RCC->AHB1ENR |= GPIOAEN |GPIOBEN |GPIOCEN;

  //(3U<<(pin*2)) sets the desired pin to (11-input mode)

  /* No pull up no pull down is (00-floating), beacuse we don't want internal resistors
    fighting sensor voltage we are trying to measure */

  // PA0,PA1,PA4,PA5,PA6 analog

  GPIOA->MODER |= 	(3U<<(0*2)) | (3U<<(1*2)) | (3U<<(4*2)) | (3U<<(5*2)) | (3U<<(6*2));
  GPIOA->PUPDR &=~	((3U<<(0*2))|(3U<<(1*2))|(3U<<(4*2))|(3U<<(5*2))|(3U<<(6*2)));

  // PB0,PB1 analog

  GPIOB->MODER |= 	(3U<<(0*2)) | (3U<<(1*2));
  GPIOB->PUPDR &=~	((3U<<(0*2))|(3U<<(1*2)));

  // PC0,PC1,PC2,PC3,PC4,PC5 analog

  for (int p=0;p<=5;p++) {
    GPIOC->MODER |= 	(3U<<((uint32_t)p*2U));
    GPIOC->PUPDR &=~	(3U<<((uint32_t)p*2U));
  }
}

static void adc_set_sampling(void) {
  const uint32_t SMP = 4U; //  (100)- 84 cycles should be a sufficient sample time code

  // ch 0 to 9 => SMPR2
  for (int ch=0; ch<=9; ch++){
    ADC1->SMPR2 &=~		(7U << (3U*ch)); 	// Clear 3 bits for this channel
    ADC1->SMPR2 |=  	(SMP << (3U*ch));	// Set those 3 bits to '100' binary which is 84 cycles
  }
  // ch 10 to 15 => SMPR1
  for (int ch=10; ch<=15; ch++){
    uint32_t s = 3U*(uint32_t)(ch-10); 		//Offset the channel number
    ADC1->SMPR1 &=~	(7U << s);				// Clear 3 bits for this channel
    ADC1->SMPR1 |=  (SMP << s);				// Set those 3 bits to '100' binary which is 84 cycles
  }
}

static void adc_set_sequence(const uint8_t *seq, uint8_t n) {

  ADC1->SQR1 = (ADC1->SQR1 & ~ADC_SQR1_L) | ((uint32_t)(n-1U) << 20); // In SQR1 bits 20-23 are for lenth
  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  //since we are sampling 13 channels we do (n-1)
  ADC1->SQR2 = 0;
  ADC1->SQR3 = 0;

  for (uint8_t i=0;i<n;i++){
    uint32_t ch = (uint32_t)(seq[i] & 0x1FU);
    if 		(i < 6)  ADC1->SQR3 |= ch << (5U*i);
    else if (i < 12) ADC1->SQR2 |= ch << (5U*(i-6U));
    else             ADC1->SQR1 |= ch << (5U*(i-12U));
  }
}

static void dma2_stream0_init(uint16_t *dst, uint32_t count) {
  RCC->AHB1ENR |= DMA2EN;			//Enable the DMA2 Clock

  //Ensure stream is disabled
  DMA2_Stream0->CR &=~ DMA_EN;
  while (DMA2_Stream0->CR & DMA_EN) {}	//waits for the hardware to confirm it is actually off

  DMA2->LIFCR = 0x3DU;  //Clears pending interrupt flags from previous operations to ensure a clean start.

  // Set Addresses
  DMA2_Stream0->PAR  = (uint32_t)& ADC1->DR; //tells the DMA to pick up data from the ADC1 Data Register
  DMA2_Stream0->M0AR = (uint32_t)dst;		 //tells the DMA to drop the data into your array (g_adc.raw)
  DMA2_Stream0->NDTR = count;				 //Sets the Number of Data Items to transfer (13 in our case).

  // Configure behaviour
  DMA2_Stream0->CR &=~	((3U<<11) | (3U<<13) | (3U<<16)); 	// clear PSIZE, MSIZE, PL
  DMA2_Stream0->CR = DMA_CIRC | DMA_MINC | DMA_PSIZE_16 | DMA_MSIZE_16 | DMA_PL_MED;

  // 6. Enable stream
  DMA2_Stream0->FCR = 0;		//Disables the FIFO buffer (Direct Mode)
  DMA2_Stream0->CR |= DMA_EN;	//Enables the stream
}

void ADC_DMA_Init(void) {
  gpio_analog_init();		//Calls our function to set the physical pins to Analog mode so they can "see" voltages.

  RCC->APB2ENR |= ADC1EN;		//Enables the clock for the ADC1 peripheral

  ADC1->CR1 = 0;		//This clears the Control Registers to a known "zero" state
  ADC1->CR2 = 0;		//This clears the Control Registers to a known "zero" state

  ADC1->CR1 |= ADC_CR1_SCAN;				//tells the ADC to measure a group of channels one after another, rather than just stopping after the first pin
  ADC1->CR2 |= ADC_CR2_CONT;				//tells the ADC to start over automatically once it finishes the last channel in the list. This creates a never-ending loop of measurements
  ADC1->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;	//Enables the DMA interface so the ADC can "talk" to the DMA controller

  adc_set_sampling();						//Sets the "look time" (84 cycles in our case) for each channel
  adc_set_sequence(g_seq, ADC_CH_COUNT);	//Tells the ADC exactly which pins to measure and in what order

  dma2_stream0_init(g_adc.raw, ADC_CH_COUNT);	//Sets up the DMA to stand by. It is now waiting for the ADC to finish a conversion so it can move the result into g_adc.raw array.

  ADC1->CR2 |= ADC_CR2_ADON;		//wakes up the ADC from its power-down state
  for (volatile int i=0;i<1000;i++) { __NOP(); }
}

void ADC_DMA_Start(void) { ADC1->CR2 |= ADC_CR2_SWSTART; }	//"Go" signal for the entire hardware chain

const adc_frame_t* ADC_GetFrame(void) { return &g_adc; }

float adc_raw_to_v(uint16_t raw) { return (raw * VREF) / ADCMAX; }



