// *************ADC.c**************
// EE445M/EE380L.6 Labs 1, 2, Lab 3, and Lab 4 
// mid-level ADC functions
// you are allowed to call functions in the low level ADCSWTrigger driver
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano Jan 5, 2020, valvano@mail.utexas.edu
#include <stdint.h>
#include "../inc/ADCSWTrigger.h"
#include "../inc/tm4c123gh6pm.h"

// channelNum (0 to 11) specifies which pin is sampled with sequencer 3
// software start
// return with error 1, if channelNum>11, 
// otherwise initialize ADC and return 0 (success)
int ADC_Init(uint32_t channelNum){
  if (channelNum > 11)
    return 1;

  enum Port {E, D, B};
  enum Port port_map[12] = {E,E,E,E,D,D,D,D,E,E,B,B};
  int port_num[12] = {3,2,1,0,3,2,1,0,5,4,4,5};

  // depending on the channel, configure a specific gpio pin
  // and set adc input to that channel
  uint32_t pin_onehot = (1 << port_num[channelNum]);

  switch (port_map[channelNum]) {
    case E:
      SYSCTL_RCGCGPIO_R |= 0x10;        //activate GPIO clock (run mode)
      GPIO_PORTE_DIR_R &= ~pin_onehot;  // set pin to input
      GPIO_PORTE_AFSEL_R |= pin_onehot; // enable alternate function
      GPIO_PORTE_DEN_R &= ~pin_onehot;  // disable digital I/O
      GPIO_PORTE_AMSEL_R |= pin_onehot; // enable analog functionality
      break;
    case D:
      SYSCTL_RCGCGPIO_R |= 0x8;         //activate GPIO clock (run mode)
      GPIO_PORTD_DIR_R &= ~pin_onehot;  // set pin to input
      GPIO_PORTD_AFSEL_R |= pin_onehot; // enable alternate function
      GPIO_PORTD_DEN_R &= ~pin_onehot;  // disable digital I/O
      GPIO_PORTD_AMSEL_R |= pin_onehot; // enable analog functionality
      break;
    case B:
      SYSCTL_RCGCGPIO_R |= 0x2;         //activate GPIO clock (run mode)
      GPIO_PORTB_DIR_R &= ~pin_onehot;  // set pin to input
      GPIO_PORTB_AFSEL_R |= pin_onehot; // enable alternate function
      GPIO_PORTB_DEN_R &= ~pin_onehot;  // disable digital I/O
      GPIO_PORTB_AMSEL_R |= pin_onehot; // enable analog functionality
      break;
  }

  SYSCTL_RCGCADC_R |= 0x0001; // enable ADC0
  while((SYSCTL_PRADC_R&0x0001) != 0x0001){};  // not sure what this is for (wait for ADC to start up?)

  ADC0_PC_R &= ~0xF;              //  clear max sample rate field
  ADC0_PC_R |= 0x1;               //  maximum speed is 125K samples/sec
  ADC0_SSPRI_R = 0x0123;          //  Sequencer 3 is highest priority
  ADC0_ACTSS_R &= ~0x0008;        //  disable sample sequencer 3
  ADC0_EMUX_R &= ~0xF000;         //  seq3 is software trigger
  ADC0_SSMUX3_R &= ~0x000F;       //  clear SS3 field
  ADC0_SSMUX3_R += channelNum;    //  set channel
  ADC0_SSCTL3_R = 0x0006;         //  no TS0 D0, yes IE0 END0
  ADC0_IM_R &= ~0x0008;           //  disable SS3 interrupts
  ADC0_ACTSS_R |= 0x0008;         //  enable sample sequencer 3
	
  return 0;
}

// software start sequencer 3 and return 12 bit ADC result
uint32_t ADC_In(void){
  ADC0_PSSI_R = 0x0008;            // 1) initiate SS3
  while((ADC0_RIS_R&0x08)==0){};   // 2) wait for conversion done
    // if you have an A0-A3 revision number, you need to add an 8 usec wait here
  uint32_t result = ADC0_SSFIFO3_R&0xFFF;   // 3) read result
  ADC0_ISC_R = 0x0008;             // 4) acknowledge completion
  return result;
}
