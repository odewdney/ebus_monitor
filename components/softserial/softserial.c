
/*
Github: junhuanchen
Copyright (c) 2018 Juwan
Licensed under the MIT license:
http://www.opensource.org/licenses/mit-license.php
*/

#include <inttypes.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>

#include <esp_clk.h>
#include <driver/gpio.h>

#define ESP8266

#ifdef ESP32
#include <soc/cpu.h>
#else
#include "esp_freertos_hooks.h"
#include "driver/soc.h"
#include "rom/ets_sys.h"
#include "esp8266/gpio_struct.h"
#endif

#include "driver/uart_select.h"
#include "softserial.h"

#define SW_EOF -1 
static const char *UART_TAG = "swuart";

struct sw_serial_tx
{
    gpio_num_t txPin;
    uint32_t bitTime;
    bool invert;
};

struct sw_serial_rx
{
    gpio_num_t rxPin;
    uint32_t buffSize, bitTime, rx_start_time, rx_end_time;
    bool invert, overflow;
    volatile uint32_t inPos, outPos;
    uint8_t *buffer;

    QueueHandle_t xQueueUart;           /*!< UART queue handler*/
    uart_select_notif_callback_t uart_select_notif_callback; /*!< Notification about select() events */
    int select_fd;
};

#ifdef ESP32
uint32_t getCycleCount()
{
    return esp_cpu_get_ccount();
}

#define WaitBitTime(wait) \
    for (uint32_t start = getCycleCount(); getCycleCount() - start < wait;)
#else

// uint32_t getCycleCount()
// {
//     uint32_t ccount;
//     __asm__ __volatile__("esync; rsr %0,ccount":"=a" (ccount));
//     return ccount;
// }


#define WaitBitTime(wait) \
    for (uint32_t start = (soc_get_ccount()); (soc_get_ccount()) - start < (wait);)

// 0.1 us == soc_get_ccount() / g_esp_ticks_per_us / 10

// #define WaitBitTime(wait) ets_delay_us(8)

static inline bool IRAM_ATTR wait_bit_state(uint8_t pin, uint8_t state, uint8_t limit)
{
    for (uint i = 0; i != limit; i++)
    {
        // ets_delay_us(1);
        WaitBitTime(limit);
        if (state == gpio_get_level(pin))
        {
            return true;
        }
    }
    // ets_delay_us(1);
    return false;
}

static inline uint8_t IRAM_ATTR check_bit_state(uint8_t pin, uint8_t limit)
{
    uint8_t flag[2] = { 0 };
    for (uint i = 0; i != limit; i++)
    {
        flag[gpio_get_level(pin)] += 1;
        ets_delay_us(1);
    }
    return flag[0] < flag[1];// flag[0] < flag[1] ? 1 : 0;
}

#define WaitBitTimeAbs(t) while(((int32_t)(soc_get_ccount()-t))<0){;}

#define int_gpio_get_level(p) (!!(GPIO.in & p))

#endif



SwSerialTx *sw_new_tx(uint32_t Tx, bool Inverse)
{
    SwSerialTx *tmp = (SwSerialTx *)calloc(1,sizeof(SwSerialTx));

    if (NULL != tmp)
    {
        tmp->txPin = Tx;
        tmp->invert = Inverse;
#ifdef ESP32
        gpio_pad_select_gpio(Tx);
        gpio_set_direction(Tx, GPIO_MODE_OUTPUT);
#else
        gpio_config_t io_conf;
        // disable interrupt
        io_conf.intr_type = GPIO_INTR_DISABLE;
        // set as output mode
        io_conf.mode = GPIO_MODE_OUTPUT;
        // bit mask of the pins that you want to set
        io_conf.pin_bit_mask = (1ULL << Tx);
        // disable pull-down mode
        io_conf.pull_down_en = GPIO_PULLUP_DISABLE;
        // disable pull-up mode
        io_conf.pull_up_en = GPIO_PULLDOWN_DISABLE;
        // configure GPIO with the given settings
        ESP_ERROR_CHECK(gpio_config(&io_conf));
#endif
        // For the TTL level of positive logic, the starting bit is the low level of one bit time.
        gpio_set_level(Tx, !Inverse); 
        // Too short leads to sticky bags
        // One byte of time 9600 104us * 10 115200 18us
        vTaskDelay(2 / portTICK_RATE_MS);
        return tmp;
    }
    return tmp;
}

void sw_del_tx(SwSerialTx *self)
{
    gpio_set_direction(self->txPin, GPIO_MODE_INPUT);
    
    free(self);
}

// suggest max datalen <= 256 and baudRate <= 115200
esp_err_t sw_open_tx(SwSerialTx *self, uint32_t baudRate)
{
    self->bitTime = esp_clk_cpu_freq() / baudRate;
    return 0;
}


int IRAM_ATTR __attribute__((optimize("O3"))) sw_write(SwSerialTx *self, uint8_t byte)
{
    gpio_set_level(12,1);

    bool invert = self->invert;
    if (invert)
    {
        byte = ~byte;
    }
    
   // Disable interrupts in order to get a clean transmit
#ifdef ESP32
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
#else
    portENTER_CRITICAL();
#endif

    // create tx interrupts to start bit.
    gpio_set_level(self->txPin, !invert);
    gpio_set_level(self->txPin, invert);

#ifdef ESP32
    WaitBitTime(self->bitTime);
#else
    // WaitBitTime(self->bitTime);
    //ets_delay_us(8 * 2); // 115200 = 8 * 1 us
    uint32_t wait_time = soc_get_ccount();
    wait_time += self->bitTime;
    WaitBitTimeAbs(wait_time);

#endif
gpio_set_level(12,0);

    for (uint8_t i = 0; i != 8; i++)
    {
        gpio_set_level(self->txPin, (byte & 1) ? 1 : 0);
#ifdef ESP32
        WaitBitTime(self->bitTime);
#else
//        ets_delay_us(8 * 2); // 115200 = 8 * 1 us
        wait_time += self->bitTime;
        WaitBitTimeAbs(wait_time);
#endif
        byte >>= 1;
    }
gpio_set_level(12,1);

    gpio_set_level(self->txPin, !invert);
#ifdef ESP32
    // Stop bit
    WaitBitTime(self->bitTime);
#else
    // Stop bit
//    ets_delay_us(8 * 2); // 115200 = 8 * 1 us
    wait_time += self->bitTime;
    WaitBitTimeAbs(wait_time);
#endif
gpio_set_level(12,0);

    wait_time += 20;
    WaitBitTimeAbs(wait_time);
gpio_set_level(12,1);

#ifdef ESP32
    // re-enable interrupts
    portEXIT_CRITICAL(&mux);
#else
    // re-enable interrupts
    portEXIT_CRITICAL();
#endif
gpio_set_level(12,0);

    return 1;
}


SwSerialRx *sw_new_rx(uint32_t Rx, bool Inverse, int buffSize)
{
    SwSerialRx *tmp = (SwSerialRx *)calloc(1,sizeof(SwSerialRx));

    if (NULL != tmp)
    {
        tmp->rxPin = Rx;
        tmp->invert = Inverse;
        tmp->buffSize = buffSize;
        tmp->buffer = (uint8_t *)malloc(buffSize);
        if (NULL != tmp->buffer)
        {
#ifdef ESP32
            gpio_pad_select_gpio(Rx);
            gpio_set_direction(Rx, GPIO_MODE_INPUT);

#else
            gpio_config_t io_conf;
            io_conf.intr_type = Inverse ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;
            io_conf.pin_bit_mask = 1ULL << Rx;
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_config(&io_conf);

#endif
io_conf.pin_bit_mask = GPIO_Pin_12;
gpio_config(&io_conf);
gpio_set_level(12,0);

            return tmp;
        }
        free(tmp), tmp = NULL;
    }

    return tmp;
}

void sw_del_rx(SwSerialRx *self)
{
    if (NULL != self->buffer)
    {
        free(self->buffer);
    }
    
    free(self);
}

void sw_set_select_notif_callback(SwSerialRx *self, int fd, uart_select_notif_callback_t uart_select_notif_callback)
{
    self->select_fd = fd;
    self->uart_select_notif_callback = uart_select_notif_callback;
}




// The first byte will wrong, after normal
static void IRAM_ATTR __attribute__((optimize("O3"))) sw_rx_handler(void *args)
{
    GPIO.out_w1ts = GPIO_Pin_12;

    SwSerialRx *self = (SwSerialRx *)args;
    uint8_t rec = 0;
    uint8_t cnt = 0;
    uint32_t rxPin = BIT(self->rxPin);

#ifdef WIN32    
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
#else
    portENTER_CRITICAL();
#endif

    // (self->invert) flag invert set Start 1 And Stop 0 invert
    // But myself not need, so need extra added by yourself

    // Wait Start Bit To Start
#ifdef ESP32
    WaitBitTime(self->rx_start_time);
    if (0 == int_gpio_get_level(rxPin))
#else
    // if (self->invert == gpio_get_level(self->rxPin))
    uint32_t wait_time = soc_get_ccount();

//    if (wait_bit_state(self->rxPin, self->invert, self->rx_start_time))
    if (self->invert == int_gpio_get_level(rxPin))
#endif
    {
        for (uint8_t i = 0; i != 8; i++)
        {
            rec >>= 1;
#ifdef ESP32
            WaitBitTime(self->bitTime);
#else
//            uint32_t tmp = self->bitTime - (i * 35);
//            WaitBitTime(tmp);
            wait_time += self->bitTime;
            WaitBitTimeAbs(wait_time);
#endif
    GPIO.out_w1tc = GPIO_Pin_12;
            if (int_gpio_get_level(rxPin))
            {
                rec |= 0x80;
            }
    GPIO.out_w1ts = GPIO_Pin_12;
        }
        // Wait Start Bit To End
#ifdef ESP32
        WaitBitTime(self->rx_end_time);
#else
        //if (wait_bit_state(self->rxPin, !self->invert, self->rx_end_time))
        wait_time += self->bitTime;
        WaitBitTimeAbs(wait_time);
#endif
        if (self->invert != int_gpio_get_level(rxPin))
        {
            // Stop bit Allow Into RecvBuffer
            // Store the received value in the buffer unless we have an overflow
            int next = (self->inPos + 1) % self->buffSize;
            if (next != self->outPos)
            {
                self->buffer[self->inPos] = (self->invert) ? ~rec : rec;
                self->inPos = next;
            }
            else
            {
                self->overflow = true;
            }
            cnt++;
        }
        GPIO.status_w1tc = BIT(self->rxPin);
    }
    
#ifdef ESP32
    portEXIT_CRITICAL(&mux);
#else
    portEXIT_CRITICAL();
#endif
    GPIO.out_w1tc = GPIO_Pin_12;
    // Must clear this bit in the interrupt register,
    // it gets set even when interrupts are disabled

    // Esp32 GPIO.status_w1tc interrupt auto recovery

    if ( cnt )
    {
        BaseType_t task_woken = 0;
        uart_event_t uart_event;
        uart_select_notif_t notify = UART_SELECT_ERROR_NOTIF;
        int uart_num = 0;

        uart_event.type = UART_EVENT_MAX;
        notify = UART_SELECT_ERROR_NOTIF;

        uart_event.type = UART_DATA;
        uart_event.size = 1;//rx_fifo_len;
        notify = UART_SELECT_READ_NOTIF;


        if (uart_event.type != UART_EVENT_MAX && self->uart_select_notif_callback) {
            self->uart_select_notif_callback(uart_num, notify, &task_woken);
            if (task_woken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }

        if (uart_event.type != UART_EVENT_MAX && self->xQueueUart) {
            if (pdFALSE == xQueueSendFromISR(self->xQueueUart, (void *)&uart_event, &task_woken)) {
                ESP_EARLY_LOGV(UART_TAG, "UART event queue full");
            }

            if (task_woken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}


esp_err_t sw_enableRx(SwSerialRx *self, bool State)
{
    esp_err_t error = ESP_OK;
    if (State)
    {
        gpio_set_intr_type(self->rxPin, (self->invert) ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
        gpio_install_isr_service(0);
        error = gpio_isr_handler_add(self->rxPin, sw_rx_handler, (void*)self);

    }
    else
    {
        error = gpio_isr_handler_remove(self->rxPin);
        gpio_uninstall_isr_service();
    }
    
    return error;
}




int sw_read(SwSerialRx *self)
{
    if (self->inPos != self->outPos)
    {
        uint8_t ch = self->buffer[self->outPos];
        self->outPos = (self->outPos + 1) % self->buffSize;
        return ch;
    }
    return -1;
}

// suggest max datalen <= 256 and baudRate <= 115200
esp_err_t sw_open_rx(SwSerialRx *self, uint32_t baudRate)
{
    // The oscilloscope told me
    self->bitTime = esp_clk_cpu_freq() / baudRate;
//printf("Starting: baud=%d bt=%u\n", baudRate, self->bitTime);

    // Rx bit Timing Settings
    switch (baudRate)
    {
        case 115200:
            self->rx_start_time = (self->bitTime / 256);
            self->rx_end_time = (self->bitTime / 256);
            break;
        
        case 9600:
            self->rx_start_time = (self->bitTime / 9);
            self->rx_end_time = (self->bitTime * 8 / 9);
            break;
        
        default: // tested 57600 len 256
            self->rx_start_time = (self->bitTime / 9);
            self->rx_end_time = (self->bitTime / 9);
            break;
    }

    //sw_write(self, 0x00); // Initialization uart link

    return sw_enableRx(self, true);
}


esp_err_t sw_stop(SwSerialRx *self)
{
    return sw_enableRx(self, false);
}

int sw_any(SwSerialRx *self)
{
    int avail = self->inPos - self->outPos;
    return (avail < 0) ? avail + self->buffSize : avail;
}

void sw_flush(SwSerialRx *self)
{
    self->inPos = self->outPos = 0;
    self->overflow = false;
}

bool sw_overflow(SwSerialRx *self)
{
    return self->overflow;
}

int sw_peek(SwSerialRx *self)
{
    if (self->inPos != self->outPos)
    {
        return self->buffer[self->outPos];
    }
    return -1;
}

