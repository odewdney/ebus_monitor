
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "softserial.h"
#include "sys/reent.h"

#include <sys/socket.h>
#include "lwip/apps/sntp.h"

const char*tag = "main";
#define TAG tag

static void initialize_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}


/*
// definition to expand macro then apply to pragma message 
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)

// Some example here
#pragma message(VAR_NAME_VALUE(SNTP_SET_SYSTEM_TIME_NTP))
#pragma message(VAR_NAME_VALUE(SNTP_SET_SYSTEM_TIME_US))
#pragma message(VAR_NAME_VALUE(SNTP_OPMODE_LISTENONLY))
*/

void initialize_sntp();
void register_sntp_cmd();


void initialize_console();
void register_console_cmd();
void run_console();
void initialise_wifi(bool flash_storage);

void start_ebus_task();

/*

static void uart_putc(int c)
{
    while (1) {
        uint32_t fifo_cnt = READ_PERI_REG(UART_STATUS(CONFIG_ESP_CONSOLE_UART_NUM)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);

        if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126)
            break;
    }

    WRITE_PERI_REG(UART_FIFO(CONFIG_ESP_CONSOLE_UART_NUM) , c);
}

int ets_putc(int c)
{
    if (ss)
        sw_write(ss, c);
    else
        uart_putc(c);
    return c;
}
*/
int     fsync (int __fd);

extern int ets_putc(int c);

#include "esp8266/uart_register.h"
static int uart_putc(int c)
{
    while (1) {
        uint32_t fifo_cnt = READ_PERI_REG(UART_STATUS(CONFIG_ESP_CONSOLE_UART_NUM)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);

        if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126)
            break;
    }

    WRITE_PERI_REG(UART_FIFO(CONFIG_ESP_CONSOLE_UART_NUM) , c);
    return c;
}

SwSerialTx *swlog = NULL;
int ets_putc(int c)
{
    if (swlog)
        sw_write( swlog, c);
    else
        return uart_putc(c);
    return c;
}

void register_ebus_cmds();

void app_main()
{
    esp_err_t err;

    ESP_LOGI(tag,"starting");

    initialize_nvs();

    register_swuart();

    fsync(fileno(stdout));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    err = uart_enable_swap();

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(tag,"swapped");

    FILE *f = fopen("/dev/swuart/0","r");
    FILE *f2 = fopen("/dev/swuart/0", "w");

//    vTaskDelay(100 / portTICK_PERIOD_MS);

    FILE *stdin_orig = stdin;
    FILE *stdout_orig = stdout;

    // _GLOBAL_REENT->_stdin
    stdin = f;
    stdout = f2;
    stderr = f2;

_GLOBAL_REENT->_stdout = f2;

// setup for ets printing
swlog = sw_new_tx(1,false);
sw_open_tx(swlog, 74880);
ets_printf("ets log\r\n");

    initialise_wifi(true);
    initialize_sntp();

    start_ebus_task();

    printf("go!\r\n");

    initialize_console();
    register_console_cmd();
    register_ebus_cmds();
    register_sntp_cmd();

/*
// UART 1 Init
uart_config_t u1 = { .baud_rate = 74880, 
    .data_bits=UART_DATA_8_BITS, .parity=UART_PARITY_DISABLE, 
    .stop_bits=UART_STOP_BITS_1, .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0 };
uart_param_config(UART_NUM_1, &u1);
uart_driver_install(UART_NUM_1, 0,0,0,NULL,0);
*/

    ESP_LOGI(tag, "starting console");
    run_console();

    vTaskDelay(100 / portTICK_PERIOD_MS);

    fclose(f);
    stdin = stdin_orig;
    stdout = stderr = stdout_orig;

    err = uart_disable_swap();
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_U0RXD);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);

    ESP_LOGI(tag,"deswaped");

    vTaskDelay(100 / portTICK_PERIOD_MS);

    printf("Done!\n");
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(tag,"end");

}



