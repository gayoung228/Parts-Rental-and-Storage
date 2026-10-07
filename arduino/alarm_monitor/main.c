/* Plain C firmware for Arduino UNO/ATmega328P. HC-05: hardware UART D0/D1. */
#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include "alarm_state.h"
#include "rental_protocol.h"

/* 0: passive buzzer, 1: active buzzer through an appropriate driver. */
#ifndef BUZZER_ACTIVE
#define BUZZER_ACTIVE 0
#endif
#define UART_BAUD 9600UL
#define UART_DIV (F_CPU / (16UL * UART_BAUD) - 1UL)
#define RX_SIZE 128U
static volatile uint8_t rx_data[RX_SIZE], rx_head, rx_tail, rx_fault;
static volatile uint8_t alarm_on;

ISR(USART_RX_vect)
{
    uint8_t status = UCSR0A;
    uint8_t ch = UDR0;
    uint8_t next = (uint8_t)((rx_head + 1U) % RX_SIZE);
    if ((status & (_BV(FE0) | _BV(DOR0) | _BV(UPE0))) || next == rx_tail) rx_fault = 1;
    else { rx_data[rx_head] = ch; rx_head = next; }
}
ISR(TIMER2_COMPA_vect)
{
    static uint16_t phase;
    if (++phase >= 8000U) phase = 0;
    if (!alarm_on || phase >= 4000U) PORTB &= (uint8_t)~_BV(PB0);
#if BUZZER_ACTIVE
    else PORTB |= _BV(PB0);
#else
    else if (phase & 2U) PORTB |= _BV(PB0);
    else PORTB &= (uint8_t)~_BV(PB0);
#endif
}
static void uart_text(const char *text)
{
    while (*text) {
        while (!(UCSR0A & _BV(UDRE0))) {}
        UDR0 = (uint8_t)*text++;
    }
}
int main(void)
{
    DDRB |= _BV(PB0) | _BV(PB5); /* D8 buzzer drive, D13 onboard alarm LED. */
    PORTB &= (uint8_t)~(_BV(PB0) | _BV(PB5));
    UBRR0H = (uint8_t)(UART_DIV >> 8);
    UBRR0L = (uint8_t)UART_DIV;
    UCSR0A = 0;
    UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    /* 8kHz interrupt, 2kHz passive buzzer tone, 0.5s ON/OFF alarm pattern. */
    TCCR2A = _BV(WGM21);
    TCCR2B = _BV(CS21);
    OCR2A = 249;
    TIMSK2 = _BV(OCIE2A);
    sei();
    uart_text("[BT_CLIENT]READY\n");
    char line[96];
    uint8_t used = 0, discard = 0;
    for (;;) {
        if (rx_fault) { alarm_state_fault(); rx_fault = 0; }
        while (rx_tail != rx_head) {
            uint8_t ch = rx_data[rx_tail];
            rx_tail = (uint8_t)((rx_tail + 1U) % RX_SIZE);
            if (ch == '\n') {
                line[used] = '\0';
                if (!discard && alarm_state_handle(line) == 1) {
                    char id[32];
                    const char *body;
                    if (rental_parse_frame(line, id, &body)) {
                        uart_text("[BT_CLIENT]ACK@"); uart_text(id); uart_text("@");
                        uart_text(body + 6); uart_text("\n");
                    }
                }
                used = discard = 0;
            } else if (ch != '\r') {
                if (!ch || used >= sizeof(line) - 1U) { discard = 1; alarm_state_fault(); }
                else if (!discard) line[used++] = (char)ch;
            }
        }
        alarm_on = (uint8_t)alarm_state_any_active();
        if (alarm_on) PORTB |= _BV(PB5);
        else PORTB &= (uint8_t)~_BV(PB5);
    }
}
