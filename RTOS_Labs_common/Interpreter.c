// *************Interpreter.c**************
// Students implement this as part of EE445M/EE380L.12 Lab 1,2,3,4 
// High-level OS user interface
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 1/18/20, valvano@mail.utexas.edu
#include <stdint.h>
#include <string.h> 
#include <stdio.h>
#include <stdlib.h>
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../inc/ADCSWTrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/heap.h"
#include "../RTOS_Lab5_ProcessLoader/loader.h"


static const ELFSymbol_t symtab[] = {
  { "ST7735_Message", ST7735_Message } // address of ST7735_Message
};

void LoadProgram(char* filename) {
  ELFEnv_t env = { symtab, 1 }; // symbol table with one entry
  if(exec_elf(filename, &env))
    printf("load failed\n");
  else
    printf("load success\n");
} 

// extern int ADCdata;
extern uint32_t maxJitter[6];
extern uint32_t jitterHistogram[6][JITTERSIZE];

extern uint32_t maxIntDisableTime;
extern uint64_t totalIntDisableTime;
extern uint64_t startTime;


// Print jitter histogram
void printJitter(int timerNum){
  printf("Jitter Info Timer %d\n", timerNum);
  printf("Max Jitter: %d\n", maxJitter[timerNum]);
  printf("Histogram:\n");
  uint32_t *hist = jitterHistogram[timerNum];
  for(int i = 0; i < JITTERSIZE; i +=4)
  {
    printf("%02d-%02d: %4d, %4d, %4d, %4d\n", i, i+3, hist[i], hist[i+1], hist[i+2], hist[i+3]);
  }
  printf("\n");
	
}

// *********** Command line interpreter (shell) ************
void Interpreter(void){ 
  char cmd[128];
  int cmdLen = 0;
  while(1)
  {
    printf(">");
    fgets(cmd, 128, stdin);
    char* cmd_tok = strtok(cmd, " \n");
    
    PD1 ^= 0x02;
    if(strcmp(cmd_tok, "lcd") == 0)
    {
      char* top_bot = strtok(NULL, " \n");
      char* line_str = strtok(NULL, " \n");
      char* value_str = strtok(NULL, " \n");
      char* message = strtok(NULL, "\n");
      if(message == NULL)
      {
        printf("usage: lcd top/bot line value word1 [wordn]\n");
        continue;
      }
      int screen;
      if(strcmp(top_bot, "top") == 0)
        screen = 0;
      else if(strcmp(top_bot, "bot") == 0)
        screen = 1;
      else
      {
        printf("usage: lcd top/bot line value word1 [wordn]\n");
        continue;
      }
      int line = atoi(line_str);
      int value = atoi(value_str);
      ST7735_Message(screen, line, message, value);

    }
    else if(strcmp(cmd_tok, "adc_in") == 0)
    {
      // printf("%d\n", ADCdata);
    }
    else if(strcmp(cmd_tok, "time") == 0)
    {
      char* arg = strtok(NULL, " \n");
      if(arg != NULL && strcmp(arg, "rst") == 0)
      {
        printf("timer reset\n");
        OS_ClearMsTime();
      }
      else if(arg != NULL && strcmp(arg, "abs") == 0)
      {
        uint64_t time = OS_Time();
        uint64_t mod = 1000 * 1000 * 1000; //1 billion
        printf("absolute time: %x%x  %u%u\n", 
               (uint32_t) (time >> 32), (uint32_t) time,
               (uint32_t) (time / mod), (uint32_t) (time % mod));
      }
      else
      {
        uint32_t time = OS_MsTime();
        printf("%u.%u\n", time / 1000, time %1000);
      }
    }
    else if(strcmp(cmd_tok, "jitter") == 0)
    {
      char* arg = strtok(NULL, " \n");
      if(arg != NULL)
      {
        printJitter(atoi(arg));
      }
      else
        printJitter(1);
    }
    else if(strcmp(cmd_tok, "int") == 0)
    {
      // float percentage = 100.0f * ((float) totalIntDisableTime / (float) (OS_Time() - startTime));
      printf("Max time ints disabled: %u us\n", maxIntDisableTime / (TIME_1MS/1000));
      printf("total time ints disabled: %u ms\n", (uint32_t) (totalIntDisableTime / TIME_1MS));
      printf("%% time ints disabled: %u%%\n", (uint32_t) ((totalIntDisableTime * 1000) / (OS_Time() - startTime)) /10);
      // printf("%% time ints disabled: %u.%03u%%\n", (uint32_t) percentage, (uint32_t) (percentage - ((int) percentage))*1000);
    }
    else if(strcmp(cmd_tok, "touch") == 0)
    {
      char* arg = strtok(NULL, " \n");
      if(arg != NULL)
      {
        if(eFile_Create(arg))
        {
          printf("create failed");
        }
        else
          printf("Created %s", arg);
      }
    }
    else if(strcmp(cmd_tok, "ls") == 0)
    {
      if(eFile_DOpen("")) 
        continue;
      char* name;
      unsigned long size;
      while(!eFile_DirNext(&name, &size)){
        printf("%s\t\t%lu\n", name, size);
        // total = total+size;
        // num++;    
      }
      // printf(string3, num);
      // printf("\n\r");
      // printf(string4, total);
      // printf("\n\r");
      if(eFile_DClose()) 
        continue;
    }
    else if(strcmp(cmd_tok, "cat") == 0)
    {
      char* arg = strtok(NULL, " \n");
      if(arg != NULL)
      {
        if(eFile_ROpen(arg))
        {
          printf("open failed");
        }
        char data;
        while(eFile_ReadNext(&data) == 0)
          printf("%c", data);

        if(eFile_RClose())
        {
          printf("close failed");
        }
      }
    }
    else if(strcmp(cmd_tok, "vim") == 0)
    {
      char* arg = strtok(NULL, " \n");
      char* data = strtok(NULL, " \n");
      if(arg != NULL && data != NULL)
      {
        if(eFile_WOpen(arg))
        {
          printf("open failed");
        }

        for(unsigned int i = 0; i < strlen(data); i++)
        {
          eFile_Write(data[i]);
        }        
        if(eFile_WClose())
        {
          printf("close failed");
        }
      }
    }
    else if(strcmp(cmd_tok, "rm") == 0)
    {
      char* arg = strtok(NULL, " \n");
      if(arg != NULL)
      {
        if(eFile_Delete(arg))
        {
          printf("delete failed");
        }
        else
          printf("Deleted %s", arg);
      }
    }
    else if(strcmp(cmd_tok, "fs_format") == 0)
    {
      eFile_Unmount();
      eFile_Format();
      eFile_Mount();
    }
    else if(strcmp(cmd_tok, "fs_mount") == 0)
      eFile_Mount();
    else if(strcmp(cmd_tok, "fs_unmount") == 0)
      eFile_Unmount();
    else if(strcmp(cmd_tok, "exec") == 0)
    {
      char* arg = strtok(NULL, " \n");
      LoadProgram(arg);
    }
    else if(strcmp(cmd_tok, "exec_batch") == 0)
    {
      char* arg_file = strtok(NULL, " \n");
      char* arg_num = strtok(NULL, " \n");
      char* arg_delay = strtok(NULL, " \n");

      if(arg_file && arg_num && arg_delay)
      {
        LoadProgram(arg_file);
        for (int i = 1; i < atoi(arg_num); ++i)
        {
          OS_Sleep(atoi(arg_delay));
          LoadProgram(arg_file);
        }

      }
    }
    else if(strcmp(cmd_tok, "heap_stats") == 0)
    {
      heap_stats_t stats;
      Heap_Stats(&stats);
      printf("size: %u\n", stats.size);
      printf("used: %u\n", stats.used);
      printf("free: %u\n", stats.free);
    }



    else
    {
      while(cmd_tok != NULL)
      {
        printf("%s\n", cmd_tok);
        cmd_tok = strtok(NULL, " \n");
      }
    }
    PD1 ^= 0x02;

  }

}


