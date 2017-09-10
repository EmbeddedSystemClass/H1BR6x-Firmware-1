/*
    BitzOS (BOS) V0.1.0 - Copyright (C) 2017 Hexabitz
    All rights reserved

    File Name     : H05R0.c
    Description   : Source code for module H05R0.
										SPI-based uSD driver with Fatfs.  
		
		Required MCU resources : 
		
			>> USARTs 1,2,3,4,5 for module ports.
			>> DMA1 Ch5, DMA1 Ch6, DMA2 Ch2 for port-to-memory messaging.
			>> DMA1 Ch1, DMA1 Ch3, DMA2 Ch3 for port-to-port streaming.
			>> SPI1 for uSD
			>> PB0 for uSD detect
			
*/
	
/* Includes ------------------------------------------------------------------*/
#include "BOS.h"



/* Define UART variables */
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart5;


/* Private variables ---------------------------------------------------------*/
log_t logs[MAX_LOGS];
logVar_t logVars[MAX_LOG_VARS];
FATFS SDFatFs;  /* File system object for SD card logical drive */
char SDPath[4]; /* SD card logical drive path */
FIL MyFile;     /* File object */
uint32_t byteswritten, bytesread;                     /* File write/read counts */
uint8_t buffer[100];
char logHeaderText1[] = "Datalog created by BOS V%d.%d.%d on %s\n\r";
char logHeaderText2[] = "Log type: Rate @ %.2f Hz\n\n\r";
char logHeaderText3[] = "Log type: Event @ %d events max\n\n\r";
uint16_t openLogs, activeLogs;
TaskHandle_t LogTaskHandle = NULL;

/* Private function prototypes -----------------------------------------------*/	
void LogTask(void * argument);
uint8_t CheckLogVarEvent(logVar_t *var);

/* Create CLI commands --------------------------------------------------------*/




/* -----------------------------------------------------------------------
	|												 Private Functions	 														|
   ----------------------------------------------------------------------- 
*/

/* --- H05R0 module initialization. 
*/
void Module_Init(void)
{
	SD_CardInfo CardInfo;
	
	/* Array ports */
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_USART4_UART_Init();
  MX_USART5_UART_Init();
	
	/* uSD - GPIO and SPI */
	if (BSP_SD_Init() == MSD_ERROR)
	{
		/* No SD card. Insert SD card and reboot */
		while(1) { IND_ON(); Delay_ms_no_rtos(500); IND_OFF(); Delay_ms_no_rtos(500); };		
	}	
	
	/* Get the uSD size and info */
	BSP_SD_GetCardInfo(&CardInfo);
	
	/* Link the micro SD disk I/O driver */
  if(FATFS_LinkDriver(&SD_Driver, SDPath) == 0)
  {
    /* Mount the default drive */
    if(f_mount(&SDFatFs, SDPath, 1) != FR_OK)
    {
			/* Unmount the drive */
			f_mount(0, SDPath, 0); 
      /* SD card malfunction. Re-insert the card and reboot */
			while(1) { IND_ON(); Delay_ms_no_rtos(500); IND_OFF(); Delay_ms_no_rtos(500); };	
    }
    else
    {		
			/* Create the logging task */
			xTaskCreate(LogTask, (const char *) "LogTask", (2*configMINIMAL_STACK_SIZE), NULL, osPriorityNormal, &LogTaskHandle);			
		}
	}
	
}
/*-----------------------------------------------------------*/

/* --- H05R0 message processing task. 
*/
Module_Status Module_MessagingTask(uint16_t code, uint8_t port, uint8_t src, uint8_t dst)
{
	Module_Status result = H05R0_OK;
	
	switch (code)
	{

		default:
			result = H05R0_ERR_UnknownMessage;
			break;
	}			

	return result;	
}

/*-----------------------------------------------------------*/

/* --- Get the port for a given UART. 
*/
uint8_t GetPort(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART4)
			return P1;
	else if (huart->Instance == USART2)
			return P2;
	else if (huart->Instance == USART3)
			return P3;
	else if (huart->Instance == USART1)
			return P4;
	else if (huart->Instance == USART5)
			return P5;
	
	return 0;
}

/*-----------------------------------------------------------*/

/* --- Logging task. 
*/
void LogTask(void * argument)
{	
	uint8_t i, j;
	//FRESULT res; char name[15] = {0}; 	
	static uint32_t rateCounter;
	
	/* Infinite loop */
	for(;;)
	{
		++rateCounter;									// Advance rate counter
		deferButtonReset = 1;						// Defer button state reset until it's logged here
		
		/* Check all active logs */
		for( j=0 ; j<MAX_LOGS ; j++)
		{	
			if ( (activeLogs >> j) & 0x01 )
			{					
//				/* Open this log file if it's closed (and close open one) */
//				if ( ((openLogs >> j) & 0x01) == 0 )
//				{
//					/* Close currently open log */
//					f_close(&MyFile);
//					/* Append log name with extension */
//					strcpy((char *)name, logs[j].name); strncat((char *)name, ".TXT", 4);
//					/* Open this log */
//					res = f_open(&MyFile, name, FA_OPEN_EXISTING | FA_WRITE | FA_READ);
//					if (res != FR_OK)	break;	
//					openLogs = (0x01 << j);
//				}		
				
				/* Check for rate or event */
				if ( (logs[j].type == RATE && rateCounter >= (configTICK_RATE_HZ/logs[j].length_rate)) || (logs[j].type == EVENT && CheckLogVarEvent(&logVars[i])) )
				{	
					rateCounter = 0;						// Reset the rate counter
					
					/* Write new line */
					f_write(&MyFile, "\n\r", 2, (void *)&byteswritten);	
					
					/* Write index */
					if (logs[j].indexColumnFormat == FMT_TIME)
					{
						;
					}
					else if (logs[j].indexColumnFormat == FMT_SAMPLE)
					{
						sprintf( ( char * ) buffer, "%d", ++(logs[j].sampleCount));
						f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);	
						memset(buffer, 0, byteswritten);
					}				

					/* Check all registered variables */
					for( i=0 ; i<MAX_LOG_VARS ; i++)
					{
						/* Write delimiter */	
						if (logVars[i].type != 0)
						{					
							if (logs[j].delimiterFormat == FMT_SPACE)
								f_write(&MyFile, " ", 1, (void *)&byteswritten);
							else if (logs[j].delimiterFormat == FMT_TAB)
								f_write(&MyFile, "\t", 1, (void *)&byteswritten);
							else if (logs[j].delimiterFormat == FMT_COMMA)
								f_write(&MyFile, ",", 1, (void *)&byteswritten);								
						}
						
						/* Write variable value */
						switch (logVars[i].type)
						{
							case PORT_DIGITAL:
								//sprintf( ( char * ) buffer, "%d", HAL_GPIO_ReadPin());
								//f_write(&MyFile, buffer, 1, (void *)&byteswritten);	
								break;
							
							case PORT_BUTTON:
								switch (button[logVars[i].source].state)
								{
									case OFF:	f_write(&MyFile, "OFF", 3, (void *)&byteswritten); break;
									case ON:	f_write(&MyFile, "ON", 2, (void *)&byteswritten); break;
									case OPEN:	f_write(&MyFile, "OPEN", 4, (void *)&byteswritten); break;
									case CLOSED:	f_write(&MyFile, "CLOSED", 6, (void *)&byteswritten); break;
									case CLICKED:	f_write(&MyFile, "CLICKED", 7, (void *)&byteswritten); break;
									case DBL_CLICKED:	f_write(&MyFile, "DBL_CLICKED", 11, (void *)&byteswritten); break;
									case PRESSED:	f_write(&MyFile, "PRESSED", 7, (void *)&byteswritten); break;
									case RELEASED:	f_write(&MyFile, "RELEASED", 8, (void *)&byteswritten); break;
									case PRESSED_FOR_X1_SEC:	
										sprintf( ( char * ) buffer, "PRESSED_FOR_%d_SEC", button[logVars[i].source].pressedX1Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									case PRESSED_FOR_X2_SEC:	
										sprintf( ( char * ) buffer, "PRESSED_FOR_%d_SEC", button[logVars[i].source].pressedX2Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									case PRESSED_FOR_X3_SEC:	
										sprintf( ( char * ) buffer, "PRESSED_FOR_%d_SEC", button[logVars[i].source].pressedX3Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									case RELEASED_FOR_Y1_SEC:	
										sprintf( ( char * ) buffer, "RELEASED_FOR_%d_SEC", button[logVars[i].source].releasedY1Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									case RELEASED_FOR_Y2_SEC:	
										sprintf( ( char * ) buffer, "RELEASED_FOR_%d_SEC", button[logVars[i].source].releasedY2Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									case RELEASED_FOR_Y3_SEC:	
										sprintf( ( char * ) buffer, "RELEASED_FOR_%d_SEC", button[logVars[i].source].releasedY3Sec);
										f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten); break;
									default: break;
								}		
								deferButtonReset = 0; 
								break;
							
							case PORT_DATA:
								
								break;
							
							case MEMORY_DATA_UINT8:
								sprintf( ( char * ) buffer, "%u", *(__IO uint8_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_INT8:
								sprintf( ( char * ) buffer, "%i", *(__IO uint8_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_UINT16:
								sprintf( ( char * ) buffer, "%u", *(__IO uint16_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_INT16:
								sprintf( ( char * ) buffer, "%i", *(__IO uint16_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_UINT32:
								sprintf( ( char * ) buffer, "%u", *(__IO uint32_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_INT32:
								sprintf( ( char * ) buffer, "%i", *(__IO uint32_t *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;

							case MEMORY_DATA_FLOAT:
								sprintf( ( char * ) buffer, "%f", *(__IO float *)logVars[i].source);
								f_write(&MyFile, buffer, strlen((const char *)buffer), (void *)&byteswritten);									
								break;
							
							default:			
								break;
						}
						
						/* Clear the buffer */
						memset(buffer, 0, byteswritten);
						
					}
				}
				break;
			}	
			else	
				break;				
		}	
		
		taskYIELD();
	}								

}

/*-----------------------------------------------------------*/

/* --- Check if a logged variable even has occured. 
				logName: Log file name.
*/
uint8_t CheckLogVarEvent(logVar_t *var)
{

	return 0;	
} 


/* -----------------------------------------------------------------------
	|																APIs	 																 	|
   ----------------------------------------------------------------------- 
*/

/* --- Create a new data log. 
				logName: Log file name. Max 10 char. If log already exists, a new column will be added.
				type: RATE or EVENT
				lengthrate: data rate in Hz (max 1000 Hz) or number of events captured.
				delimiterFormat: FMT_SPACE, FMT_TAB, FMT_COMMA
				indexColumn: FMT_SAMPLE, FMT_TIME
				indexColumnLabel: Index Column label text. Max 30 char.
*/
Module_Status CreateLog(const char* logName, logType_t type, float lengthrate, delimiterFormat_t delimiterFormat, indexColumnFormat_t indexColumnFormat,\
	const char* indexColumnLabel)
{
	FRESULT res; char name[15] = {0};
	uint8_t i=0;
	
	/* Check if log already exists */
	for( i=0 ; i<MAX_LOGS ; i++)
  {
		if(!strcmp(logs[i].name, logName))
		{
			return H05R0_ERR_LogNameExists;
		}
  }
	
	/* Check parameters are correct */
	if ( (type != RATE && type != EVENT)	||
			 (delimiterFormat != FMT_SPACE && delimiterFormat != FMT_TAB && delimiterFormat != FMT_COMMA)	||
			 (indexColumnFormat != FMT_NONE && indexColumnFormat != FMT_SAMPLE && indexColumnFormat != FMT_TIME)	||
			 (lengthrate > 1000) )
		return H05R0_ERR_WrongParams;				
					
	/* Name does not exist. Fill first empty location */
	for( i=0 ; i<MAX_LOGS ; i++)
  {
		if(logs[i].name == 0)
		{
			/* Append log name with extension */
			strcpy((char *)name, logName); strncat((char *)name, ".TXT", 4);
			/* Check if file exists on disk */
			res = f_open(&MyFile, name, FA_CREATE_NEW | FA_WRITE | FA_READ);
			if(res == FR_EXIST)
				return H05R0_ERR_LogNameExists;
			else if (res != FR_OK)
				return H05R0_ERR_SD;	
			
			/* Log created successfuly */
			logs[i].name = logName;
			logs[i].type = type;
			logs[i].length_rate = lengthrate;
			logs[i].delimiterFormat = delimiterFormat;
			logs[i].indexColumnFormat = indexColumnFormat;
			logs[i].indexColumnLabel = indexColumnLabel;
			
			/* Write log header */
			strncpy(name, modulePNstring[4], 5); name[5] = 0;				// Copy only module PN
			sprintf( ( char * ) buffer, logHeaderText1, _firmMajor, _firmMinor, _firmPatch, name);
			res = f_write(&MyFile, buffer, sizeof(logHeaderText1), (void *)&byteswritten);
			memset(buffer, 0, byteswritten);
			if(type == RATE) {
				sprintf( ( char * ) buffer, logHeaderText2, lengthrate);
				res = f_write(&MyFile, buffer, sizeof(logHeaderText2), (void *)&byteswritten);				
			} else if (type == EVENT) {
				sprintf( ( char * ) buffer, logHeaderText3, (uint32_t)lengthrate);
				res = f_write(&MyFile, buffer, sizeof(logHeaderText3), (void *)&byteswritten);	
			}
			memset(buffer, 0, byteswritten);
			//f_close(&MyFile);
			
			/* Write index label */
			res = f_write(&MyFile, indexColumnLabel, strlen(indexColumnLabel), (void *)&byteswritten);
							
			return H05R0_OK;
		}
  }	

	return H05R0_ERR_MaxLogs;	
}

/*-----------------------------------------------------------*/

/* --- Save data from a port to an existing data log. 
				logName: Log file name.
				type: PORT_DIGITAL, PORT_DATA, PORT_BUTTON, MEMORY_DATA.
				source: data source. Ports (P1-Px), buttons (B1-Bx) or memory location.
				columnLabel: Column label text. Max 30 char.
*/
Module_Status LogVar(const char* logName, logVarType_t type, uint32_t source, const char* ColumnLabel)
{
	uint8_t i = 0, j = 0;

	/* Search for this log to make sure it exists */
	for( j=0 ; j<MAX_LOGS ; j++)
	{
		if (!strcmp(logs[j].name, logName))
		{
			/* Make sure there's enough space for this log variable */
			for( i=0 ; i<MAX_LOG_VARS ; i++)
			{
				if(logVars[i].type == 0)
				{
					logVars[i].type = type;
					logVars[i].source = source;
					logVars[i].logIndex = j;
					logVars[i].varLabel = ColumnLabel;
					
					/* Write delimiter */
					if (logs[j].delimiterFormat == FMT_SPACE)
						f_write(&MyFile, " ", 1, (void *)&byteswritten);
					else if (logs[j].delimiterFormat == FMT_TAB)
						f_write(&MyFile, "\t", 1, (void *)&byteswritten);
					else if (logs[j].delimiterFormat == FMT_COMMA)
						f_write(&MyFile, ",", 1, (void *)&byteswritten);
					/* Write variable label */
					f_write(&MyFile, ColumnLabel, strlen(ColumnLabel), (void *)&byteswritten);
					
					return H05R0_OK;
				}
			}
			return H05R0_ERR_MaxLogVars;
		}
	}		

	return H05R0_ERR_LogDoesNotExist;	
}

/*-----------------------------------------------------------*/

/* --- Start an existing data log. 
				logName: Log file name.
*/
Module_Status StartLog(const char* logName)
{
	uint8_t j = 0;

	/* Search for this log to make sure it exists */
	for( j=0 ; j<MAX_LOGS ; j++)
	{
		if (!strcmp(logs[j].name, logName))
		{
			activeLogs |= (0x01 << j);
			logs[j].sampleCount = 0;
			return H05R0_OK;
		}		
	}

	return H05R0_ERR_LogDoesNotExist;	
}

/*-----------------------------------------------------------*/

/* --- Stop a running data log. 
				logName: Log file name.
*/
Module_Status StopLog(const char* logName)
{
	uint8_t j = 0;

	/* Search for this log to make sure it exists */
	for( j=0 ; j<MAX_LOGS ; j++)
	{
		if (!strcmp(logs[j].name, logName))
		{
			if ( (activeLogs >> j) & 0x01 )
			{
				activeLogs &= ~(0x01 << j);
				logs[j].sampleCount = 0;
				/* Close log file */
				f_close(&MyFile);
				return H05R0_OK;
			}
			else
				return H05R0_ERR_LogIsNotActive;
		}		
	}
	
	return H05R0_ERR_LogDoesNotExist;	
}

/*-----------------------------------------------------------*/

/* --- Pause a running data log. 
				logName: Log file name.
*/
Module_Status PauseLog(const char* logName)
{
	uint8_t j = 0;

	/* Search for this log to make sure it exists */
	for( j=0 ; j<MAX_LOGS ; j++)
	{
		if (!strcmp(logs[j].name, logName))
		{
			if ( (activeLogs >> j) & 0x01 )
			{
				activeLogs &= ~(0x01 << j);
				return H05R0_OK;
			}
			else
				return H05R0_ERR_LogIsNotActive;
		}		
	}

	return H05R0_ERR_LogDoesNotExist;	
} 

/*-----------------------------------------------------------*/

/* --- Resume a paused data log. 
				logName: Log file name.
*/
Module_Status ResumeLog(const char* logName)
{
	uint8_t j = 0;

	/* Search for this log to make sure it exists */
	for( j=0 ; j<MAX_LOGS ; j++)
	{
		if (!strcmp(logs[j].name, logName))
		{
			activeLogs |= (0x01 << j);
			return H05R0_OK;
		}		
	}

	return H05R0_ERR_LogDoesNotExist;	
} 

/*-----------------------------------------------------------*/

/* --- Delete an existing data log. 
				logName: Log file name.
				options: DELETE_ALL, KEEP_ON_DISK
*/
Module_Status DeleteLog(const char* logName, options_t options)
{
	Module_Status result = H05R0_OK;


	return result;	
}

/*-----------------------------------------------------------*/

/* -----------------------------------------------------------------------
	|															Commands																 	|
   ----------------------------------------------------------------------- 
*/



/*-----------------------------------------------------------*/


/************************ (C) COPYRIGHT HEXABITZ *****END OF FILE****/
