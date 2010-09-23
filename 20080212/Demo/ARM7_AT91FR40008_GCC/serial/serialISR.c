/*
  FreeRTOS.org V4.7.1 - Copyright (C) 2003-2008 Richard Barry.

  This file is part of the FreeRTOS.org distribution.

  FreeRTOS.org is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  FreeRTOS.org is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FreeRTOS.org; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  A special exception to the GPL can be applied should you wish to distribute
  a combined work that includes FreeRTOS.org, without being obliged to provide
  the source code for any proprietary components.  See the licensing section 
  of http://www.FreeRTOS.org for full details of how and when the exception
  can be applied.

  ***************************************************************************
  See http://www.FreeRTOS.org for documentation, latest information, license 
  and contact details.  Please ensure to read the configuration and relevant 
  port sections of the online documentation.
  ***************************************************************************
*/


/* 
  BASIC INTERRUPT DRIVEN SERIAL PORT DRIVER FOR USART0. 

  This file contains all the serial port components that must be compiled
  to ARM mode.  The components that can be compiled to either ARM or THUMB
  mode are contained in serial.c.

*/

/* Standard includes. */
#include <stdlib.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Demo application includes. */
#include "serial.h"
#include "AT91R40008.h"
#include "usart.h"

/*-----------------------------------------------------------*/

/* Constant to access the AIC. */
#define serCLEAR_AIC_INTERRUPT      ( ( unsigned portLONG ) 0 )

/* Constants to determine the ISR source. */
#define serSOURCE_THRE				( ( unsigned portCHAR ) 0x02 )
#define serSOURCE_RX_TIMEOUT		( ( unsigned portCHAR ) 0x0c )
#define serSOURCE_ERROR				( ( unsigned portCHAR ) 0x06 )
#define serSOURCE_RX				( ( unsigned portCHAR ) 0x04 )
#define serINTERRUPT_SOURCE_MASK    ( ( unsigned portLONG ) (US_RXRDY | US_TXRDY | US_RXBRK | US_OVRE | US_FRAME | US_PARE) )

/* Queues used to hold received characters, and characters waiting to be
transmitted. */
static xQueueHandle xRxedChars; 
static xQueueHandle xCharsForTx; 

/*-----------------------------------------------------------*/

/* UART0 interrupt service routine.  This can cause a context switch so MUST
be declared "naked". */
void vUART_ISR_Wrapper( void ) __attribute__ ((naked));

/* The ISR function that actually performs the work.  This must be separate 
from the wrapper to ensure the correct stack frame is set up. */
void vUART_ISR_Handler( void );

/*-----------------------------------------------------------*/
void vSerialISRCreateQueues( unsigned portBASE_TYPE uxQueueLength, xQueueHandle *pxRxedChars, xQueueHandle *pxCharsForTx )
{
	/* Create the queues used to hold Rx and Tx characters. */
	xRxedChars = xQueueCreate( uxQueueLength, ( unsigned portBASE_TYPE ) sizeof( signed portCHAR ) );
	xCharsForTx = xQueueCreate( uxQueueLength + 1, ( unsigned portBASE_TYPE ) sizeof( signed portCHAR ) );

	/* Pass back a reference to the queues so the serial API file can 
	post/receive characters. */
	*pxRxedChars = xRxedChars;
	*pxCharsForTx = xCharsForTx;
}
/*-----------------------------------------------------------*/

void vUART_ISR_Wrapper( void )
{
	/* Save the context of the interrupted task. */
	portSAVE_CONTEXT();

	/* Call the handler.  This must be a separate function to ensure the 
	stack frame is correctly set up. */
	vUART_ISR_Handler();

	/* Restore the context of whichever task will run next. */
	portRESTORE_CONTEXT();
}
/*-----------------------------------------------------------*/

void vUART_ISR_Handler( void )
{
/* Now we can declare the local variables.   These must be static. */
signed portCHAR cChar;
portBASE_TYPE xTaskWokenByTx = pdFALSE, xTaskWokenByRx = pdFALSE;
unsigned portLONG ulStatus;

	/* What caused the interrupt? */
	ulStatus = AT91C_BASE_US0->US_CSR & AT91C_BASE_US0->US_IMR;

	if (ulStatus & US_TXRDY)
	{
		/* The interrupt was caused by the THR becoming empty.  Are there any
		more characters to transmit? */
		if( xQueueReceiveFromISR( xCharsForTx, &cChar, &xTaskWokenByTx ) == pdTRUE )
		{
			/* A character was retrieved from the queue so can be sent to the
			THR now. */
			AT91C_BASE_US0->US_THR = cChar;
		}
		else
		{
			/* Queue empty, nothing to send so turn off the Tx interrupt. */
			AT91C_BASE_US0->US_IDR = US_TXRDY;
		}    
	}

	if (ulStatus & US_RXRDY)
	{
		/* The interrupt was caused by the receiver getting data. */
		cChar = AT91C_BASE_US0->US_RHR;

		if (xQueueSendFromISR(xRxedChars, &cChar, pdFALSE))
		{
			xTaskWokenByRx = pdTRUE;
		}
	}

	/* Acknowledge the interrupt at AIC level... */
	AT91C_BASE_AIC->AIC_EOICR = serCLEAR_AIC_INTERRUPT;

	/* If an event caused a task to unblock then we call "Yield from ISR" to
	ensure that the unblocked task is the task that executes when the interrupt
	completes if the unblocked task has a priority higher than the interrupted
	task. */
	if( xTaskWokenByTx || xTaskWokenByRx )
	{
		portYIELD_FROM_ISR();
	}
}
/*-----------------------------------------------------------*/
