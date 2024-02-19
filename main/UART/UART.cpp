#include "UART.h"
UART::UART()
{
}

UART::UART(uart_port_t port, uart_config_t *config, int rx_pin, int tx_pin, int rts_pin, int cts_pin, int bufferSize)
{
    this->uart_num = port;
    this->uart_config = *(config);
    this->pins.rx_pin = rx_pin;
    this->pins.tx_pin = tx_pin;
    this->pins.cts_pin = cts_pin;
    this->pins.rts_pin = rts_pin;
    this->buffer = (uint8_t *)malloc(bufferSize * 2 * sizeof(uint8_t));
    this->bufferSize = bufferSize;
}

UART::~UART()
{
    free(buffer);
}

UART &UART::operator=(const UART &)
{
    return *this;
}

void UART::init(void)
{
    ESP_ERROR_CHECK(uart_param_config(this->uart_num, &this->uart_config));
    ESP_ERROR_CHECK(uart_set_pin(this->uart_num, this->pins.tx_pin, this->pins.rx_pin, this->pins.cts_pin, this->pins.rts_pin));
    ESP_ERROR_CHECK(uart_driver_install(this->uart_num, this->bufferSize, this->bufferSize, 10, &(this->uart_queue), 0));
    uart_flush(this->uart_num);
}

void UART::flush(void){
    vTaskDelay(10/portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(uart_flush(this->uart_num));
}

void UART::sendChar(uint8_t x)
{
    uart_write_bytes(this->uart_num, &x, 1);
}

uint8_t UART::recieveChar(void)
{
    return 0;
}

uint8_t UART::recieveChar(uint8_t timeout)
{
    return 0;
}

uint8_t UART::recieveString(uint8_t* buff, int maxLen, uint8_t timeout)
{
    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(this->uart_num, (size_t *)&length));
    length = uart_read_bytes(this->uart_num, buff, maxLen-1, timeout);
    ESP_ERROR_CHECK(uart_flush(this->uart_num));
    return length;
}

void UART::sendBytes(uint8_t *data, uint8_t len)
{
    uart_write_bytes(this->uart_num, data, len);
}

void UART::send(const char *data)
{
}

void UART::send(long num)
{
}

void UART::send(unsigned long num)
{
}

void UART::send(int num)
{
}

void UART::send(unsigned int num)
{
}