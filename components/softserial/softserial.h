#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/uart_select.h"
#include <driver/gpio.h>

typedef struct sw_serial_tx SwSerialTx;
typedef struct sw_serial_rx SwSerialRx;

SwSerialTx *sw_new_tx(gpio_num_t Tx, bool Inverse);
void sw_del_tx(SwSerialTx *self);
esp_err_t sw_open_tx(SwSerialTx *self, uint32_t baudRate);
int sw_write(SwSerialTx *self, uint8_t byte);

SwSerialRx *sw_new_rx(gpio_num_t Rx, bool Inverse, int buffSize);
void sw_del_rx(SwSerialRx *self);
esp_err_t sw_open_rx(SwSerialRx *self, uint32_t baudRate);
int sw_read(SwSerialRx *self);
esp_err_t sw_stop(SwSerialRx *self);
int sw_any(SwSerialRx *self);

void sw_set_select_notif_callback(SwSerialRx *self, int fd, uart_select_notif_callback_t uart_select_notif_callback);

void register_swuart();


#ifdef __cplusplus
}
#endif
