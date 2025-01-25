#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"

#include "ebus.h"
#include "ebus_dev.h"

const static char *TAG="ebusd";
#define LOG_FMT(x) x

class EbusdMonitor : public EbusMonitor
{
    TaskHandle_t ebusdTask;
    TimerHandle_t ebusdTimer;

    int fd_sock = -1;
    EbusSender *sender = nullptr;
    EbusMessageWriter msg;
    uint8_t prev = 0;

    enum class EbusdState {
        Idle, CommandSend, ResponseACK, ResponseSYN
    } ebusdState = EbusdState::Idle; 

public:
    EbusdMonitor(EbusSender *_sender)
    {
        sender = _sender;
    }

    void sendCmd(uint8_t cmd, uint8_t data)
    {
        if (cmd != 1 || data >= 0x80) {
            uint8_t txbuf[2];
            txbuf[0] = 0xc0 | (cmd<<2) | ((data>>6)&3);
            txbuf[1] = 0x80 | (data & 0x3f);
            send(fd_sock, txbuf, 2, 0);
        } else {
            send(fd_sock, &data, 1, 0);
        }
    }

    void ProcessClient()
    {
        uint8_t buf[10];

        int len = recv(fd_sock, buf, sizeof(buf)-1, 0);
        if (len == 0) {
            close(fd_sock);
            fd_sock = -1;
            return;
        }
        if (len < 0) {
            return;
        }
        for (int n = 0; n < len; n++) {
            uint8_t c = buf[n];
            uint8_t t = c & 0xc0;
            if ( t == 0xc0) {
                if ( prev != 0)
                    printf("unexpected 1st %02x have prev %02x\r\n", c, prev);
                prev = c;
            } else {
                uint8_t cmd;
                if ( t == 0x80) {
                    if ( prev == 0) {
                        printf("unexpected 2nd %02x\r\n", c);
                        cmd = 100;
                    } else {
                        cmd = (prev>>2) & 0xf;
                        c = (c&0x3f) | ((prev&3)<<6);
//                        ESP_LOGI(TAG,"Cmd %d data %02x", cmd, c);
                        prev = 0;
                    }
                } else {
                    if ( prev != 0) {
                        printf("unexpected raw %02x have prev %02x\r\n", c, prev);
                        cmd = 100;
                    } else {
//                        printf("char %02x\r\n", c);
                        cmd = 1; // just data
                    }
                }

                switch(cmd) {
                    case 0: //reset
                        sendCmd(0, 1);
                        break;
                    case 1: // data
                        switch (ebusdState) {
                            case EbusdState::CommandSend:
                            {
                                auto end = msg.Write(c);
                            
                                sendCmd(1,c);

                                if (end) {
                                    ESP_LOGI(TAG, "Sending packet len %d", msg.GetBufferLength());
                                    if ( sender )
                                        sender->Send(msg);
                                    ebusdState = msg.GetDest() == BROADCAST_ADDR ? EbusdState::Idle : EbusdState::ResponseACK;
                                    msg.Reset();
                                }
                            }
                            break; 
                        case EbusdState::ResponseACK:
                            sendCmd(1,c);
                            ebusdState = EbusdState::ResponseSYN;
                            break;
                        case EbusdState::ResponseSYN:
                            sendCmd(1,c);
                            ebusdState = EbusdState::Idle;
                            break;
                        default:
                            ESP_LOGI(TAG, "Unexpected data %02x in state %d", c, (int)ebusdState);
                        }
                        break;
                    case 2: //start
                        sendCmd(2,c);
                        msg.Reset();
                        if ( c != 0xaa) {
                            msg.Write(c);
                            ebusdState = EbusdState::CommandSend;
                        } else {
                            ESP_LOGI(TAG, "Start reset");
                            ebusdState = EbusdState::Idle;
                        }
                        break;
                    case 3: // info
                        if (c==0){
                            const static uint8_t infobuf[18] = {0xcc, 0x88, 
                                0xcc, 0x91, 0xcc, 0x80, 0xcc, 0x81, 0xcc, 0xb2, 
                                0xcc, 0x81, 0xcc, 0x92, 0xcc, 0x83, 0xcc, 0x84};
                            send(fd_sock, infobuf, 18,0);
                        }
                        break;
                    default:
                        ESP_LOGI(TAG,"Cmd %d data %02x", cmd, c);
                        break;

                }
            }
        }

    }

    static void ebussocket_worker(void*arg)
    {
        auto p = (EbusdMonitor*)arg;
        p->ebussocket_worker();
    }

    void ebussocket_worker()
    {
        int fd = socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            ESP_LOGE(TAG, LOG_FMT("error in socket (%d)"), errno);
            return;
        }

        int ret;
        
        {
        int enable = 1;
        ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        }

        {
        struct sockaddr_in serv_addr = {
            .sin_len = sizeof(struct sockaddr_in),
            .sin_family   = PF_INET,
            .sin_port     = htons(9999),
            .sin_addr     = {
                .s_addr = htonl(INADDR_ANY)
            },
            .sin_zero = {0}
        };
        ret = bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }

        if (ret < 0) {
            ESP_LOGE(TAG, LOG_FMT("error in bind (%d)"), errno);
            close(fd);
            return;
        }

        ret = listen(fd, 2);
        if (ret < 0) {
            ESP_LOGE(TAG, LOG_FMT("error in listen (%d)"), errno);
            close(fd);
            return;
        }

        while(true)
        {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(fd, &read_set);
            int fdMax = fd;
            if (fd_sock != -1) {
                FD_SET(fd_sock, &read_set);
                if ( fd_sock > fdMax)
                    fdMax = fd_sock;
            }

            int ret = select(fdMax+1, &read_set, NULL, NULL, NULL);
            if ( ret < 0)
                break;
            if (fd_sock != -1 && FD_ISSET(fd_sock, &read_set)) {
                ProcessClient();
            }
            if(FD_ISSET(fd, &read_set)) {
                if ( fd_sock != -1) {
                    close(fd_sock);
                    fd_sock = -1;
                }
                struct sockaddr_in addr_from;
                socklen_t addr_from_len = sizeof(addr_from);
                int new_fd = accept(fd, (struct sockaddr *)&addr_from, &addr_from_len);
                if (new_fd < 0) {
                    ESP_LOGW(TAG, LOG_FMT("error in accept (%d)"), errno);
                } else {
                    fd_sock = new_fd;   
                }
            }

        }

    }

    static void ebusd_timercb(TimerHandle_t xTimer)
    {
        auto p = (EbusdMonitor*)pvTimerGetTimerID(xTimer);
        p->ebusd_timercb();
    }


    int timeout = 0;
    void ebusd_timercb()
    {
        if (fd_sock != -1 ) {
            if ( ebusdState == EbusdState::Idle) {
                static const uint8_t buf[2] = {0xc6, 0xaa}; // syn recv
                send(fd_sock, buf, 2, 0);
            } else {
                timeout++;
                if ( timeout > 5) {
                    timeout = 0;
                    ebusdState = EbusdState::Idle;
                    ESP_LOGI(TAG, "SYN timeout");
                }
            }
        }
    }

    void Notify(EbusMessage const &msg)
    {}

    void Notify(EbusMessage const &msg, EbusResponse const &response)
    {
        if ( fd_sock == -1) return;

        uint8_t buf[100];
        int pos = 0;

        buf[pos++] = ACK;
        auto m = response.GetBufferLength();
        auto p = response.GetBuffer();
        for(int n = 0; n < m; n++)
        {
            auto c = *p++;
            if ( c < 0x80)
                buf[pos++] = c;
            else {
                buf[pos++] = 0xc0 | (1<<2) | ((c>>6) & 3);
                buf[pos++] = 0x80 | (c & 0x3f);
            }
        }

        send(fd_sock, buf, pos, 0);
ESP_LOGI(TAG, "Responded %d->%d", m,pos);
    }
    
    void start()
    {
        xTaskCreate(ebussocket_worker, "ebusd", 2000, this, 5, &ebusdTask);
        ebusdTimer = xTimerCreate( "ebus-ack", 200 / portTICK_PERIOD_MS, true, this, ebusd_timercb);
        xTimerStart(ebusdTimer, 0);
    }

};

EbusMonitor *initialise_ebusd(EbusSender *_sender)
{
    auto ret = new EbusdMonitor(_sender);
    ret->start();
    return ret;

}