// *************Interpreter.c**************
// Students implement this as part of EE445M/EE380L.12 Lab 1,2,3,4 
// High-level OS user interface
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 1/18/20, valvano@mail.utexas.edu
#include <stdint.h>
#include <string.h> 
#include <stdio.h>
#include <limits.h>
#include "OS.h"
#include "../utils/ustdlib.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../inc/ADCSWTrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/ADC.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Lab5_processloader/loader.h"


// Print jitter histogram
void Jitter(int32_t MaxJitter, uint32_t const JitterSize, uint32_t JitterHistogram[]){
  // write this for Lab 3 (the latest)
	
}

// predefined string hashes
#define LCD_TOP    1804049322U
#define LCD_BOTTOM 3179955660U
#define ADC_IN     4050446275U
#define TIME       2090760340U
#define RESET_TIME 2371262582U
#define HELP       2090324718U
#define DATA_LOST  99240257U
#define INTERRUPT  314333426U
#define HELP_MESSAGE "Help:\r\n\
lcd_top <line> <message>: 		print to <line> on the top half of the lcd\r\n\
lcd_bottom <line> <message>: 	print to <line> on the bottom half of the lcd\r\n\
adc_in: 											gets the current value of ADC (channel 3)\r\n\
time: 												gets the current runtime in ms\r\n\
reset_time: 									resets the runtime counter\r\n\
help: 												prints this message\r\n\
threads <line>: 							displays thread count\r\n\
jitter <line>: 								displays jitter time\r\n\
datalost <line>: 							displays datalost\r\n\
interrupt: 										displays percentage interrupt enabled time and longest interrupt\r\n\
format: 											format SD card\r\n\
display_dir: 									display files in dir \r\n\
print_file <string>: 					print file with name \r\n\
delete_file <string>: 				delete file with name \r\n\
create_file <string>: 				create empty file with name \r\n\
append_file <string> <char>:  append to file with char \r\n\
load <file>:                 	executes ELF/AXF file.\r\n\
"
#define THREADS 3758332912U //threads
#define JITTER 114260791U //jitter

//Filesys instructions
#define FORMAT 673136283 //format
#define DISPLAY_DIR 2509617850 //display_dir
#define PRINT_FILE 2403744957 //print_file
#define DELETE_FILE 1348838026 //delete_file
#define CREATE_FILE 2565874136 //create_file
#define WOPEN_FILE 1536931917 //wopen_file
#define APPEND_FILE 1750019708 //append_file
#define WCLOSE_FILE 2509654161 //wclose_file

//Elf file
#define LOAD       2090478981U //load

//Extern variable for debugging
//extern uint32_t DataLost; 
uint32_t DataLost = 0; 

// process loader
static const ELFSymbol_t symtab[] = {
  { "ST7735_Message", ST7735_Message }
};
static ELFEnv_t env = {symtab, 1};

// djb2 hash
const unsigned int hash(const char *str) {
  unsigned long hash = 5381;
  int c;
  while (c = *str++) {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }
  return hash % UINT_MAX; // (4294967296)
}

// *********** Command line interpreter (shell) ************
void Interpreter(void){ //OS suspend Fifo stuff maybe
  // write this  
  uint16_t maxstrlen = 30; //Stack allocated for a TCB needs to be big enough to accomodate this
  char strbuf[maxstrlen]; //Command passed to interpreter
  char msg_out[maxstrlen]; //Message to print out
  char msg_in[maxstrlen]; //Param to pass in (string)
  uint32_t line = 0;
  uint32_t lcdval = 0;
	char lcdchar = 0;
	char charArg = 0; //Char param

  UART_OutString("\rEnter a command:\r\n");

  while (1) {
    UART_OutString("> ");
    strbuf[0] = '\0';
    UART_InString(strbuf, maxstrlen); //Hardfault causing line fixed: Increase TCB stack size to avoid overflow
    UART_OutString("\r\n");

    // do nothing on empty line
    if (strbuf[0] == '\0')
      continue;

    //Parse inputs
    char *args = strchr(strbuf, ' ')+1;
    char *cmd = strtok(strbuf, " ");

    switch (hash(cmd)) {
      case LCD_TOP:
        // parse arguments
        if(sscanf(args, "%d %s %d", &line, msg_in, &lcdval) == 3){ 
          // print to screen
          ST7735_Message(0,line,msg_in,lcdval);
          usnprintf(msg_out, maxstrlen, "Printed to line %d of top screen: '%s' %d\r\n", line, msg_in, lcdval);
          UART_OutString(msg_out);
        } else {
          UART_OutString("Error lcd_top: wrong arguments\r\n");
        }
        break;

      case LCD_BOTTOM:
        // parse arguments
        if(sscanf(args, "%d %s %d", &line, msg_in, &lcdval) == 3){ 
          // print to screen
          ST7735_Message(1,line,msg_in,lcdval);
          usnprintf(msg_out, maxstrlen, "Printed to line %d of bottom screen: '%s' %d\r\n", line, msg_in, lcdval);
          UART_OutString(msg_out);
        } else {
          UART_OutString("Error lcd_top: wrong arguments\r\n");
        }
        break;

      case ADC_IN:
        uint32_t adcval = ADC_In();
        usnprintf(strbuf, maxstrlen, "ADC value: %d\r\n", adcval);
        UART_OutString(strbuf);
        break;

      case TIME:
        uint32_t systime = OS_MsTime();
        usnprintf(msg_out, maxstrlen, "Current System Time: %d ms\r\n", systime);
        UART_OutString(msg_out);
        break;

      case RESET_TIME:
        OS_ClearMsTime();
        UART_OutString("Reset system timer.\r\n");
        break;

      case HELP:
        UART_OutString(HELP_MESSAGE);
        break;

      case THREADS:
				if(sscanf(args, "%d", &line) == 1){ 
					ST7735_Message(0,line,"Thread Count: ", threadCount);
					UART_OutUDec(threadCount);
				} else {
          UART_OutString("Error threads: wrong arguments\r\n");
        }
				
        break;

      case JITTER:
				if(sscanf(args, "%d", &line) == 1){ 
					ST7735_Message(0,line,"Jitter (0.1 us): ", MaxJitter);
					UART_OutUDec(MaxJitter);
				} else {
          UART_OutString("Error jitter: wrong arguments\r\n");
        }
        break;
				
			case DATA_LOST:
				if(sscanf(args, "%d", &line) == 1){ 
					ST7735_Message(0,line,"DataLost: ", DataLost);
					UART_OutUDec(DataLost);
				} else {
          UART_OutString("Error: datalost: wrong arguments\r\n");
        }
        break;
		
			case INTERRUPT:
				ST7735_Message(0,1,"Percent: ", percentInterruptDisabled());
				UART_OutUDec(percentInterruptDisabled());
				ST7735_Message(0,2,"longest: ", longestInterruptDisabled());
				UART_OutUDec(longestInterruptDisabled());
        break;
			
			case FORMAT:
				int res = eFile_Format();
				if(res == 0){
					UART_OutString("Formatted disk\r\n");
				} else {
					UART_OutString("Format fail\r\n");
				}
				break;
			
			case DISPLAY_DIR:
				char* pt;
				unsigned long size;
				//Try to open dir
				if(eFile_DOpen(NULL) == 1){break;}
				//Keep reading until end
				while(eFile_DirNext(&pt, &size) == 0){
					UART_OutString("name: ");
					UART_OutString(pt);
					UART_OutString(" size: ");
					UART_OutUDec(size);  
					UART_OutString("\r\n");  
				}
				//Close dir
				eFile_DClose();
				break;
			
			case PRINT_FILE:
				//Read open, print, and then Read close within one command
				if(sscanf(args, "%s", msg_in) == 1){ 
					//Try to open
					if(eFile_ROpen(msg_in) != 0){
						UART_OutString("Error: print_file: can't open file\r\n");
						break;
					}
					
					//Read characters
					char data;
					while(eFile_ReadNext(&data) == 0){
						UART_OutChar(data); //Keep reading characters from file
					}
					UART_OutString("\r\n");
					
					//End finished read, close file
					eFile_RClose();
				} else {
          UART_OutString("Error: print_file: wrong arguments\r\n");
        }
				break;
			
			case DELETE_FILE:
				if(sscanf(args, "%s", msg_in) == 1){ 
					//Try to delete
					if(eFile_Delete(msg_in) != 0){
						UART_OutString("Error: delete_file: can't delete file\r\n");
						break;
					} else {
						UART_OutString("Deleted file\r\n");
					}
				} else {
          UART_OutString("Error: delete_file: wrong arguments\r\n");
        }
				break;
				
			case CREATE_FILE:
				if(sscanf(args, "%s", msg_in) == 1){ 
					//Test name length
					if(strlen(msg_in) > 7){
						UART_OutString("Error: create_file: exceed max length of name (7)\r\n");
						break;
					}
					//Try to create
					if(eFile_Create(msg_in) != 0){
						UART_OutString("Error: create_file: can't create file\r\n");
						break;
					} else {
						UART_OutString("Created file\r\n");
					}
				} else {
          UART_OutString("Error: Create_file: wrong arguments\r\n");
        }
				break;
			
			case WOPEN_FILE:
				if(sscanf(args, "%s", msg_in) == 1){ 
					//Try to write open
					if(eFile_WOpen(msg_in) != 0){
						UART_OutString("Error: wopen_file: can't open file\r\n");
						break;
					} else {
						UART_OutString("WOpen file\r\n");
					}
				} else {
          UART_OutString("Error: wopen_file: wrong arguments\r\n");
        }
				break;
			
			case WCLOSE_FILE:
				if(sscanf(args, "%s", msg_in) == 1){ 
					//Try to write close
					if(eFile_WOpen(msg_in) != 0){
						UART_OutString("Error: wclose_file: can't close file\r\n");
						break;
					} else {
						UART_OutString("WClose file\r\n");
					}
				} else {
          UART_OutString("Error: wclose_file: wrong arguments\r\n");
        }
				break;
			
			case APPEND_FILE:
				if(sscanf(args, "%c", &charArg) == 1){ 
					//Try to append close
					if(eFile_Write(charArg) != 0){
						UART_OutString("Error: append_file: can't write to file\r\n");
						break;
					} else {
						UART_OutString("Write to file\r\n");
					}
				} else {
          UART_OutString("Error: append_file: wrong arguments\r\n");
        }
				break;
        
      default:
        usnprintf(msg_out, maxstrlen, "Error: unknown command '%s'.\r\n", cmd);
        UART_OutString(msg_out);
        break;
			
			case LOAD:
        if(sscanf(args, "%s", msg_in) != 1){
          UART_OutString("Error: invalid number of arguments\r\n");
          break;
        }
        if (eFile_ROpen(msg_in)) {
          UART_OutString("Error opening file for reading\r\n");
          break;
        }

        UART_OutString("Loading file ");
        UART_OutString(msg_in);
        UART_OutString("\r\n");
        
        if (!exec_elf(msg_in, &env)) {
          UART_OutString("Error loading file\r\n");
          break;
        }
        UART_OutString("Success\r\n");
        break;
    }
  }
}
