/*!
 * Low-level USART interface.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include "hardware/usart.h"

#define F_CPU_DIV_8	(F_CPU/8)
#define F_CPU_DIV_16	(F_CPU/16)

/*! Compute approximate ratio between f_cpu and baud */
#define UBRR_VAL(f_cpu, baud)	\
	(((f_cpu) + ((baud)/2))/(baud))

/*! Compute effective baud rate */
#define UBRR_ERR(f_cpu, div, baud)	\
	(((f_cpu) * (div)) - (baud))

/*! Handler for transmit data */
static void usart_txfifo_evth(struct fifo_t* const fifo, uint8_t events);

/*!
 * Initialise USART
 */
int8_t usart_init(uint32_t baud, uint16_t mode) {
	/* Compute baud rate prescaler, try both /8 and /16 modes */
	uint32_t ubrr_div16 = UBRR_VAL(F_CPU_DIV_16, baud);
	uint32_t ubrr_div8 = UBRR_VAL(F_CPU_DIV_8, baud);

	/* If we have neither option, fail now */
	if (!ubrr_div16 && !ubrr_div8)
		return -1;

	/* If we have both options, pick the closest */
	if (ubrr_div16 && ubrr_div8) {
		int32_t err_div16 = UBRR_ERR(F_CPU_DIV_16,
				ubrr_div16, baud);
		int32_t err_div8 = UBRR_ERR(F_CPU_DIV_8,
				ubrr_div8, baud);
		if (err_div16 < 0)
			err_div16 = -err_div16;
		if (err_div8 < 0)
			err_div8 = -err_div8;
		if (err_div16 > err_div8)
			ubrr_div16 = 0;	/* Ignore /16 */
	}

	/* Shut everything down first */
	UCSR1B = 0;

	UCSR1C	= (((mode >> 14) & 0x03) << UMSEL10)	/* USART mode */
		| (((mode >> 5) & 0x03) << UPM10)	/* Parity mode */
		| (((mode >> 8) & 0x01) << USBS1)	/* Stop bits */
		| (((mode >> 9) & 0x03) << UCSZ10)	/* Frame size */
		| (((mode >> 7) & 0x01) << UCPOL1);	/* SCK polarity */

	UBRR1 = (ubrr_div16) ? ubrr_div16 : ubrr_div8;
	UCSR1A = (ubrr_div16) ? 0 : U2X1;
	UCSR1B |= ((mode & USART_MODE_RXEN) /* Receive settings */
			? ((1 << RXCIE1) | (1 << RXEN1))
			: 0)
		| ((mode & USART_MODE_TXEN) /* Transmit settings */
			? ((1 << TXCIE1) | (1 << TXEN1) | (1 << UDRIE1))
			: 0)
		| (((mode >> 11) & 0x01) << UCSZ12);

	/* Set up FIFO handler */
	usart_fifo_tx.consumer_evth = usart_txfifo_evth;
	usart_fifo_tx.consumer_evtm = FIFO_EVT_NEW;
	return 0;
}

static void usart_txfifo_evth(struct fifo_t* const fifo, uint8_t events) {
	if (events & FIFO_EVT_NEW) {
		/* Kick the transmit done interrupt */
		UCSR1A |= (1 << TXC1);
	}
}

static void usart_send_next() {
	/* Ready to send next byte */
	int16_t byte = fifo_read_one(&usart_fifo_tx);
	if (byte >= 0)
		UDR1 = byte;
}

ISR(USART1_RX_vect) {
	fifo_write_one(&usart_fifo_rx, UDR1);
}
ISR(USART1_TX_vect) {
	usart_send_next();
}
ISR(USART1_UDRE_vect) {
	usart_send_next();
}
