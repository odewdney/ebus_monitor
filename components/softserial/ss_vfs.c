
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>

#include "driver/uart_select.h"
#include "softserial.h"
#include "esp8266/uart_register.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"


#include "freertos/task.h"

#define NONE -1

typedef struct {
    SwSerialRx *uartRx;
    SwSerialTx *uartTx;
    bool non_blocking;
    uint8_t rx_mode, tx_mode;
    int16_t peek_char;
} vfs_uart_context_t;

static const char*tag="ssvfs";

#define NUM_CTX 5
vfs_uart_context_t s_context[NUM_CTX] = {};

static int swuart_open(const char * path, int flags, int mode)
{
//    ESP_LOGI(tag,"opening");
    vfs_uart_context_t *ctx = NULL;
    int fd;
    for(fd = 0; fd < NUM_CTX; fd++)
    {
        if (s_context[fd].uartRx == NULL && s_context[fd].uartTx == NULL)
        {
            ctx = &s_context[fd];
            break;
        }
    }
    if (ctx == NULL)
    {
        ESP_LOGE(tag,"no more ctx");
        errno = ENOENT;
        return -1;
    }

    gpio_num_t tx=15,rx=13;
    if(strcmp(path,"/0")==0 || 1) { tx=1; rx=3; }
ESP_LOGI(tag,"Opening flags=%x mode=%x", flags,mode);
flags+=1;
    if (flags & _FREAD) {
        ctx->uartRx = sw_new_rx(rx,false, 100);
        sw_open_rx(ctx->uartRx, 74880);
    }
    if (flags & _FWRITE) {
        ctx->uartTx = sw_new_tx(tx,false);
        sw_open_tx(ctx->uartTx, 74880);
    }
    ctx->non_blocking = ((flags & O_NONBLOCK) == O_NONBLOCK);
    ctx->rx_mode = ctx->tx_mode = ESP_LINE_ENDINGS_LF;
    ctx->peek_char = NONE;
    return fd;
}

static int swuart_close(int fd)
{
    vfs_uart_context_t *ctx = &s_context[fd];
    if (ctx->uartTx) {
        sw_del_tx(ctx->uartTx);
        ctx->uartTx = NULL;
    }
    if (ctx->uartRx) {
        sw_stop(ctx->uartRx);
        sw_del_rx(ctx->uartRx);
        ctx->uartRx = NULL;
    }
    return 0;
}

static ssize_t swuart_write(int fd, const void * data, size_t size)
{
    vfs_uart_context_t *ctx = &s_context[fd];
    if(ctx->uartTx == NULL) return -1;
    const char *data_c = (const char *)data;
    /*  Even though newlib does stream locking on each individual stream, we need
     *  a dedicated UART lock if two streams (stdout and stderr) point to the
     *  same UART.
     */
    for (size_t i = 0; i < size; i++) {
        int c = data_c[i];

        if (c == '\n' && ctx->tx_mode != ESP_LINE_ENDINGS_LF) {
            sw_write(ctx->uartTx, '\r');
            if (ctx->tx_mode == ESP_LINE_ENDINGS_CR) {
                continue;
            }
        }

        sw_write(ctx->uartTx, c);
    }
//    ESP_LOGI(tag, "written=%d",size);
    return size;
}

static int swuart_read_char(vfs_uart_context_t *ctx)
{
    /* return character from peek buffer, if it is there */
    if (ctx->peek_char != NONE) {
        int c = ctx->peek_char;
        ctx->peek_char = NONE;
        return c;
    }
    return sw_read(ctx->uartRx);
}

static void swuart_return_char(vfs_uart_context_t *ctx, int c)
{
    assert(ctx->peek_char == NONE);
    ctx->peek_char = c;
}

static ssize_t swuart_read(int fd, void* data, size_t size)
{
    vfs_uart_context_t *ctx = &s_context[fd];
    char *data_c = (char *) data;
    size_t received = 0;
    while (received < size) {
        int c = swuart_read_char(ctx);
        
        if (c == '\r') {
            if (ctx->rx_mode == ESP_LINE_ENDINGS_CR) {
                c = '\n';
            } else if (ctx->rx_mode == ESP_LINE_ENDINGS_CRLF) {
                // look ahead
                int c2 = sw_read(ctx->uartRx);
                if (c2 == NONE) {
                    // could not look ahead, put the current character back
                    swuart_return_char(ctx, c);
                    break;
                }
                if (c2 == '\n') {
                    // this was \r\n sequence. discard \r, return \n
                    c = '\n';
                } else {
                    // \r followed by something else. put the second char back,
                    // it will be processed on next iteration. return \r now.
                    swuart_return_char(ctx, c2);
                }
            }
        } else if (c == NONE) {
            if (received == 0 && !ctx->non_blocking) {
                // wait until serial event
                vTaskDelay(1);
                continue;
            }
            break;
        }
        data_c[received] = (char) c;
        ++received;
//        if (c == '\n') {
//            break;
//        }
    }
    if (received > 0) {
        return received;
    }
    errno = EWOULDBLOCK;
    return -1;
}


typedef struct {
    esp_vfs_select_sem_t select_sem;
    fd_set *readfds;
    fd_set *writefds;
    fd_set *errorfds;
    fd_set readfds_orig;
    fd_set writefds_orig;
    fd_set errorfds_orig;
} uart_select_args_t;

static uart_select_args_t **s_registered_selects = NULL;
static int s_registered_select_num = 0;
static int s_registered_select_size = 0;

static esp_err_t register_select(uart_select_args_t *args)
{
    esp_err_t ret = ESP_ERR_INVALID_ARG;

    if (args) {
        portENTER_CRITICAL();
        const int new_size = s_registered_select_num + 1;
        if (new_size > s_registered_select_size &&
          ((s_registered_selects = realloc(s_registered_selects, new_size * sizeof(uart_select_args_t *))) == NULL ||
          (s_registered_select_size = new_size) == 0)) { // assignment on success
            ret = ESP_ERR_NO_MEM;
        } else {
            s_registered_selects[s_registered_select_num] = args;
            s_registered_select_num = new_size;
            ret = ESP_OK;
        }
        portEXIT_CRITICAL();
    }

    return ret;
}

static esp_err_t unregister_select(uart_select_args_t *args)
{
    esp_err_t ret = ESP_OK;
    if (args) {
        ret = ESP_ERR_INVALID_STATE;
        portENTER_CRITICAL();
        for (int i = 0; i < s_registered_select_num; ++i) {
            if (s_registered_selects[i] == args) {
                const int new_size = s_registered_select_num - 1;
                // The item is removed by overwriting it with the last item.
                if (new_size > 0)
                    s_registered_selects[i] = s_registered_selects[new_size];
                s_registered_select_num = new_size;
                ret = ESP_OK;
                break;
            }
        }
        portEXIT_CRITICAL();
    }
    return ret;
}

static void select_notif_callback_isr(uart_port_t uart_num, uart_select_notif_t uart_select_notif, BaseType_t *task_woken)
{
    portENTER_CRITICAL();
    for (int i = 0; i < s_registered_select_num; ++i) {
        uart_select_args_t *args = s_registered_selects[i];
        if (args) {
            switch (uart_select_notif) {
                case UART_SELECT_READ_NOTIF:
                    if (FD_ISSET(uart_num, &args->readfds_orig)) {
                        FD_SET(uart_num, args->readfds);
                        esp_vfs_select_triggered_isr(args->select_sem, task_woken);
                    }
                    break;
                case UART_SELECT_WRITE_NOTIF:
                    if (FD_ISSET(uart_num, &args->writefds_orig)) {
                        FD_SET(uart_num, args->writefds);
                        esp_vfs_select_triggered_isr(args->select_sem, task_woken);
                    }
                    break;
                case UART_SELECT_ERROR_NOTIF:
                    if (FD_ISSET(uart_num, &args->errorfds_orig)) {
                        FD_SET(uart_num, args->errorfds);
                        esp_vfs_select_triggered_isr(args->select_sem, task_woken);
                    }
                    break;
            }
        }
    }
    portEXIT_CRITICAL();
}

static esp_err_t uart_start_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
        esp_vfs_select_sem_t select_sem, void **end_select_args)
{
    int max_fds = MIN(FD_SETSIZE, NUM_CTX);
    *end_select_args = NULL;

    uart_select_args_t *args = malloc(sizeof(uart_select_args_t));

    if (args == NULL) {
        return ESP_ERR_NO_MEM;
    }

    args->select_sem = select_sem;
    args->readfds = readfds;
    args->writefds = writefds;
    args->errorfds = exceptfds;
    args->readfds_orig = *readfds; // store the original values because they will be set to zero
    args->writefds_orig = *writefds;
    args->errorfds_orig = *exceptfds;
    FD_ZERO(readfds);
    FD_ZERO(writefds);
    FD_ZERO(exceptfds);

    portENTER_CRITICAL();

    //uart_set_select_notif_callback sets the callbacks in UART ISR
    for (int i = 0; i < max_fds; ++i) {
        if (FD_ISSET(i, &args->readfds_orig) || FD_ISSET(i, &args->writefds_orig) || FD_ISSET(i, &args->errorfds_orig)) {
            vfs_uart_context_t *ctx = &s_context[i];
            sw_set_select_notif_callback(ctx->uartRx, i, select_notif_callback_isr);
        }
    }

    for (int i = 0; i < max_fds; ++i) {
        if (FD_ISSET(i, &args->readfds_orig)) {
            vfs_uart_context_t *ctx = &s_context[i];
            if (sw_any(ctx->uartRx)) {
                // signalize immediately when data is buffered
                FD_SET(i, readfds);
                esp_vfs_select_triggered(args->select_sem);
            }
        }
    }

    esp_err_t ret = register_select(args);
    if (ret != ESP_OK) {
        portEXIT_CRITICAL();
        free(args);
        return ret;
    }

    portEXIT_CRITICAL();

    *end_select_args = args;
    return ESP_OK;
}

static esp_err_t uart_end_select(void *end_select_args)
{
    uart_select_args_t *args = end_select_args;
    int max_fds = MIN(FD_SETSIZE, NUM_CTX);

    portENTER_CRITICAL();
    esp_err_t ret = unregister_select(args);
    for (int i = 0; i < max_fds; ++i) {
        bool selected = false;
        for( int j = 0; j < s_registered_select_num; j++) {
            uart_select_args_t *args = s_registered_selects[j];
            if (FD_ISSET(i, &args->readfds_orig) || FD_ISSET(i, &args->writefds_orig) || FD_ISSET(i, &args->errorfds_orig)) {
                selected = true;
                break;
            }
        }
        if (!selected) {
            vfs_uart_context_t *ctx = &s_context[i];
            sw_set_select_notif_callback(ctx->uartRx, i, NULL);
        }
    }
    portEXIT_CRITICAL();

    if (args) {
        free(args);
    }

    return ret;
}

static int swuart_fstat(int fd, struct stat * st)
{
    st->st_mode = S_IFCHR;
    return 0;
}

static int swuart_fcntl(int fd, int cmd, int arg)
{
    vfs_uart_context_t *ctx = &s_context[fd];
    int result = 0;
    if (cmd == F_GETFL) {
        if (ctx->non_blocking) {
            result |= O_NONBLOCK;
        }
    } else if (cmd == F_SETFL) {
        ctx->non_blocking = (arg & O_NONBLOCK) != 0;
        //const char* msg =ctx->non_blocking ? "nonblock\r\n" : "blocking\r\n"; 
        //swuart_write(fd, msg, strlen(msg));
    } else {
        // unsupported operation
        result = -1;
        errno = ENOSYS;
    }
    return result;
}

#ifdef CONFIG_VFS_SUPPORT_TERMIOS
static int swuart_tcsetattr(int fd, int optional_actions, const struct termios *p)
{
    vfs_uart_context_t *ctx = &s_context[fd];

    if (p->c_iflag & IGNCR) {
        ctx->rx_mode = ESP_LINE_ENDINGS_CRLF;
    } else if (p->c_iflag & ICRNL) {
        ctx->rx_mode = ESP_LINE_ENDINGS_CR;
    } else {
        ctx->rx_mode = ESP_LINE_ENDINGS_LF;
    }

    if (p->c_oflag & ONLCR)
        ctx->tx_mode = ESP_LINE_ENDINGS_CRLF;
    else if (p->c_oflag & ONLRET)
        ctx->tx_mode = ESP_LINE_ENDINGS_CR;
    else
        ctx->tx_mode = ESP_LINE_ENDINGS_LF;

    return 0;
}

static int swuart_tcgetattr(int fd, struct termios *p)
{
    vfs_uart_context_t *ctx = &s_context[fd];
    memset(p, 0, sizeof(struct termios));

    if (ctx->rx_mode == ESP_LINE_ENDINGS_CRLF) {
        p->c_iflag |= IGNCR;
    } else if (ctx->rx_mode == ESP_LINE_ENDINGS_CR) {
        p->c_iflag |= ICRNL;
    }

    if (ctx->tx_mode == ESP_LINE_ENDINGS_CRLF) {
        p->c_oflag |= ONLCR;
    } else if (ctx->tx_mode == ESP_LINE_ENDINGS_CR) {
        p->c_oflag |= ONLRET;
    }

    p->c_cflag |= CS8;
    return 0;
}


#endif

void register_swuart()
{
    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = &swuart_write,
        .open = &swuart_open,
        .fstat = &swuart_fstat,
        .close = &swuart_close,
        .read = &swuart_read,
        .fcntl = &swuart_fcntl,

        .start_select = &uart_start_select,
        .end_select = &uart_end_select,

#ifdef CONFIG_VFS_SUPPORT_TERMIOS
        .tcsetattr = &swuart_tcsetattr,
        .tcgetattr = &swuart_tcgetattr,
#endif
    };

    ESP_ERROR_CHECK(esp_vfs_register("/dev/swuart", &vfs, NULL));

}
