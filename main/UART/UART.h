#ifndef __UART_H__
#define __UART_H__

#include "driver/uart.h"
#include "driver/gpio.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include <iostream>

using namespace std;

class UART
{
private:
    typedef struct pins_setup_struct {
        int rx_pin = UART_PIN_NO_CHANGE;
        int tx_pin = UART_PIN_NO_CHANGE;
        int rts_pin = UART_PIN_NO_CHANGE;
        int cts_pin = UART_PIN_NO_CHANGE;
    } pin_setup;

    uart_port_t uart_num;
    uart_config_t uart_config;
    uint8_t* buffer;
    int bufferSize;
    pin_setup pins;
    QueueHandle_t uart_queue;
public:
    UART();
    UART(uart_port_t port, uart_config_t* config, int rx_pin, int tx_pin, int rts_pin, int cts_pin, int bufferSize = 1024);
    ~UART();

    UART& operator= (const UART&);


    void init(void);
    void flush(void);

	void sendChar(uint8_t x);
	uint8_t recieveChar(void);
	uint8_t recieveChar(uint8_t timeout);
    uint8_t recieveString(uint8_t* buff, int maxLen, uint8_t timeout);

	void sendBytes(uint8_t* data, uint8_t length = 0);
	void send( const char* data);
	void send( long num);
	void send( unsigned long num);
	void send( int num);
	void send( unsigned int num);
};

#endif