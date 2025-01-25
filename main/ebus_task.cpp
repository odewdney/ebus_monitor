
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/uart.h"
#include "esp8266/uart_register.h"

#include "ebus.h"
#include "ebus_dev.h"
#include "ebus_device.h"

#include "argtable3/argtable3.h"
#include "esp_console.h"

#include <queue>

#include "lwip/apps/sntp.h"

#define SYN_Time 140
#define SYN_Timeout(m) ((masterAddress * 10 + 10 + SYN_Time)/ portTICK_PERIOD_MS)

static const char* TAG ="EBUS";

// 0 1 3 7 f
#define EBUS_ADDR(id,pri) (  (((1<<id)-1)<<4) | ((0x1<<pri)-1) )
#define EBUS_SLAVE_ADDR(addr) ((uint8_t)(addr+5))

uint8_t masterAddress = EBUS_ADDR(2,1); // 0x71
uart_port_t uart_num = UART_NUM_0;
uint8_t lock_max = 5;
uint8_t lock_counter;


int err_mastertolong = 0;
int err_reqnak = 0;
int err_notack = 0;

void printhex(const char* msg, const uint8_t*buf, int len)
{
    printf("%s:", msg);
    for(int n=0;n<len;n++)
        printf("%02x", buf[n]);
    printf("\r\n");
}

bool IS_MASTER(uint8_t c)
{
    char c1 = c&0xf;
    if (c1 != 0 && c1 != 1 && c1 != 3 && c1 != 7 && c1 != 0xf)
        return false;
    c1 = (c>>4)&0xf;
    if (c1 != 0 && c1 != 1 && c1 != 3 && c1 != 7 && c1 != 0xf)
        return false;
    return true;
}



class EbusDeviceDebug : public EbusDeviceBase, public EbusSender
{
public:
    EbusDeviceDebug(uint8_t addr, EbusBus *b)
        : EbusDeviceBase( addr, 0x10, "DBG01", 0x102, 0x304, b)
    {}

    void ProcessBroadcastMessage(EbusMessage const &msg)
    {
        if (msg.GetCmd() == 0xb516) {
            auto data = msg.GetPayload();
            switch (data[0]) {
                case 0:
                {
                    struct tm t = {
                        .tm_sec=msg.ReadPayloadBCD(1), .tm_min=msg.ReadPayloadBCD(2), .tm_hour=msg.ReadPayloadBCD(3),
                        .tm_mday=msg.ReadPayloadBCD(4), .tm_mon=msg.ReadPayloadBCD(5)-1, .tm_year=msg.ReadPayloadBCD(7)+100,
                        .tm_wday=(data[6] & 7) % 7, .tm_yday=0, .tm_isdst=0
                    };
                    char buf[40];
                    strftime(buf, sizeof(buf), "%c", &t);

                    printf("Broadcast datetime: %s\r\n", buf);
                    return;
                }
                case 1:
                    auto outsideTemp = msg.ReadPayloadData2b(1);
                    printf("Broadcast outside:%d.%d\r\n", (int)outsideTemp, ((int)(outsideTemp*10))%10 );
                    return;
            }

        }
        EbusDevice::ProcessBroadcastMessage(msg);
        if ( monitor )
            monitor->Notify(msg);
    }
    
    bool ProcessResponse(EbusMessage const &msg, EbusResponse const &response)
    {
        if ( monitor )
            monitor->Notify(msg, response);
        // we like everything
        return true;
    }

    EbusMonitor *monitor = nullptr;

    void Send(EbusMessage const &msg)
    {
        // clone msg
        auto sendMsg = new EbusMessage(msg);
        bus->QueueMessage(sendMsg);
    }

    bool ProcessTimer(int cnt)
    {
        if ((cnt % 60)  == 1){
            auto m = sntp_get_sync_status();
            if ( m == SNTP_SYNC_STATUS_COMPLETED) {

                time_t now = time(0);
                struct tm *t = localtime(&now);

                char buf[40];
                strftime(buf, sizeof(buf), "%c", t);

                ESP_LOGI(name, "SNTP sync %s", buf);

                // B:Req: 10 fe b516 (8) Data: 00 46 10 15 05 09 04 24 (cb)
                // Broadcast datetime: Thu Sep  5 15:10:46 2024

                auto msg = new EbusMessage(masterAddress, BROADCAST_ADDR, 0xb516);
                msg->AddPayload(0);
                msg->AddPayloadBCD(t->tm_sec);
                msg->AddPayloadBCD(t->tm_min);
                msg->AddPayloadBCD(t->tm_hour);
                msg->AddPayloadBCD(t->tm_mday);
                msg->AddPayloadBCD(t->tm_mon+1);
                msg->AddPayloadBCD(t->tm_wday);
                msg->AddPayloadBCD(t->tm_year % 100);
                msg->SetCRC();
                bus->QueueMessage(msg);
            }
        }

        return EbusDeviceBase::ProcessTimer(cnt);
    }

};


class EbusBusStream : public EbusBusData
{
    TaskHandle_t ebusTask;
    
    TimerHandle_t synTimer;
    bool synMaster = false;
    uint32_t synTime;

    static void SynSendTimerCallback(TimerHandle_t xTimer)
    {
        auto bus = (EbusBusStream*)pvTimerGetTimerID(xTimer);
        bus->SynSendTimerCallback();
    }

    void SynSendTimerCallback()
    {
        if ( !synMaster ) {
            ESP_LOGI(TAG, "becoming SYN");
            xTimerChangePeriod( synTimer, SYN_Time /portTICK_PERIOD_MS, 0 );
            synMaster = true;
        }
        SendSYN();
        synTime = esp_log_early_timestamp();
    }

protected:
    virtual void SendData(const uint8_t *data, int len) = 0;
    virtual int ReadByte() = 0;

    void SendSYN()
    {
        SendChar(SYN);
    }

    void SynRecieved()
    {
        if ( synMaster ) {
            // has someone else sent a syn
            uint32_t now = esp_log_early_timestamp();
            if ((now-synTime) > 10) {
                synMaster = false;
                xTimerChangePeriod( synTimer, SYN_Timeout(masterAddress), 0);
                ESP_LOGI(TAG, "recevied other SYN");
            }
        }
    }

    void SynRetrigger()
    {
        xTimerReset(synTimer, 0);
    }

    std::queue<const EbusMessage*> cmd_queue;
public:
    void QueueMessage(const EbusMessage *msg)
    {
        if (cmd_queue.size() > 10) {
            ESP_LOGI(TAG, "queue full");
            delete msg;
            return;
        }
        cmd_queue.push(msg);
    }


    void start()
    {
        TickType_t synTimeout = SYN_Timeout(masterAddress);
        synTimer = xTimerCreate("syn", synTimeout, true, this, SynSendTimerCallback);
        xTimerStart( synTimer, 0);

        xTaskCreate(ebusTaskCallback, "ebus", 2000, this, 5, &ebusTask);

    }

    static void ebusTaskCallback(void *args)
    {
        auto bus = (EbusBusStream*)args;
        bus->ebusTaskCallback();
    }

    void ebusTaskCallback();

};


void EbusBusStream::ebusTaskCallback()
{
    ESP_LOGI(TAG,"ebus starting");

    for(auto dev : devices) {
        ESP_LOGI(TAG,"starting %s", dev->GetName());
        dev->start();
    }

//    printf("ebus printf\r\n");

    EbusMessageWriter request;
    EbusResponseWriter response;
    
    bool esc = false;
    uint8_t state = 0;

    EbusMessage const *cmd = nullptr;
    int cmd_retry = 0;

    while(true) {
        int data = ReadByte();
        if ( data >= 0)
        {
            uint8_t c = (uint8_t)data;
            if (c==SYN) {
                int oldstate = state;
                state = 0;
                if ( lock_counter == 0 ) {
                    if (cmd == nullptr && !cmd_queue.empty()) {
                        cmd = cmd_queue.front();
                        cmd_queue.pop();
                    }
                    if (cmd != nullptr) {
                        // send Source - arb
                        SendChar(cmd->GetSource());
                        //lock_counter = lock_max;
                        state = 100;
                    }
                } else {
                    lock_counter--;
                }
                SynRecieved();

                switch (oldstate) {
                    case 0:
                        {
                            if (!request.IsEmpty()) {
                                printf("State:%d\r\n", oldstate);
                                printf("e: ");
                                request.print();
                            }
                        } 
                        break;
                    case 1:
                        printf("No Slave Ack\r\n");
                        break;
                    case 2:
                        printf("Failed response len=%d\r\n", response.GetWrittenLen());
                        if (response.GetWrittenLen() > 0)
                            response.print();
                        break;
                    case 3:
                        printf("No Master Ack\r\n");
                        break;
                    case 98: // expected end
                    case 99: // already errored
                        break;
                    default:
                        printf("Unexpected state %d\r\n", oldstate);
                        break;
                }
                request.Reset();
                response.Reset();
            } else if (c == ESC) {
                esc = true;
            } else {
                if ( esc ) {
                    if ( c == 0 )
                        c = ESC;
                    else if (c==1)
                        c = SYN;
                    esc = false;
                }
                switch(state)
                {
                    case 0:  // SS DD C1 C2 0L DD* CC
                        {
                        auto req = request.Write(c);
                        if (req) {
                            if (!request.IsValidCRC()) {
                                printf("X:");
                                request.print();
                                ESP_LOGI(TAG, "bad CRC");
                                state = 99;
                            }
                            else if (request.GetDest() == BROADCAST_ADDR ) {
                                printf("B:");
                                request.print();
                                ProcessMessage(request);
                                state = 98;
                            }
                            else {
                                printf("r:");
                                request.print();
                                ProcessMessage(request);
                                state = 1;
                            }
                        }
                        }
                        break;
                    case 1: //  ack
                        if (c == ACK) {
                            if (IS_MASTER(request.GetDest())) {
                                //printhex("m", request, req_len);
                                state = 98;
                            } else {
                                state = 2;
                            }
                        }
                        else if (c==NAK) {
                            ESP_LOGI(TAG, "NAKed");
                            err_reqnak++;
                            state = 99;
                        } else {
                            ESP_LOGE(TAG, "not ack %02x", c);
                            err_notack++;
                            state = 99;
                        }
                        break;
                    case 2: // client req
                        {
                        auto res = response.Write(c);
                        if (res) {
                            if (!response.IsValidCRC()) {
                                ESP_LOGE(TAG, "resp bad");
                                state = 99;
                            } else {
                                printf("  c:");
                                response.print();
                                ProcessResponse(request, response);
                                state = 3;
                            }
                        }
                        }
                        break;
                    case 3: // ack
                        if (c == ACK)
                            state = 98;
                        else {
                            ESP_LOGE(TAG, "Not ack for response %02x", c);
                            state = 99;
                        }
                        break;
                    case 98:
                        ESP_LOGI(TAG, "unexpected data %02x", c);
                        break;
                    case 100:
                        request.Write(c);
                        state = 0;
                        if (c == cmd->GetSource()) {
                            // we won arb
                            SendData(cmd->GetBuffer() + 1, cmd->GetBufferLength()-1);
                            // TODO
                            delete cmd;
                            cmd = nullptr;
                        } else {
                            ESP_LOGI(TAG, "Failed arb %02x %02x", c, cmd->GetSource());
                            lock_counter = lock_max;
                            if ( cmd_retry-- == 0) {
                                delete cmd;
                                cmd = nullptr;
                            }
                        }
                        break;
                }

            }

            SynRetrigger();

        } else if (data < -1) {
            ESP_LOGE(TAG, "uart read failed");
            break;
        }

    }
}


class EbusBusUart : public EbusBusStream
{
    uart_port_t uart_num;

    void SendData(const uint8_t*buf, int len)
    {
        uart_tx_chars(uart_num, (const char*)buf, len);
    } 

    int ReadByte()
    {
        uint8_t c;
        int len = uart_read_bytes(uart_num, &c, 1, 40/portTICK_PERIOD_MS);
        if (len == 1)
            return c;
        if (len == 0)
            return -1;
        return -2; // error
    }
public:
    EbusBusUart(uart_port_t port)
    {
        uart_num = port;
    }
};

EbusBus *bus;


class EbusDeviceVr91 : public EbusDeviceBase
{
public:
    EbusDeviceVr91(uint8_t masterAddr, EbusBus *bus)
        : EbusDeviceBase(masterAddr, 0xb5, "VR_91", 0x0200, 0x1903, bus)
    {

    }
};



void start_ebus_task()
{
    uart_config_t uart_conf = { .baud_rate = 2400, 
    .data_bits=UART_DATA_8_BITS, .parity=UART_PARITY_DISABLE, 
    .stop_bits=UART_STOP_BITS_1, .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0 };

    // dont use config func - as it sets the pins
    uart_set_baudrate(uart_num, uart_conf.baud_rate);
    uart_set_word_length(uart_num, uart_conf.data_bits);
    uart_set_stop_bits(uart_num, uart_conf.stop_bits);
    uart_set_parity(uart_num, uart_conf.parity);
    uart_set_hw_flow_ctrl(uart_num, uart_conf.flow_ctrl, uart_conf.rx_flow_ctrl_thresh);
    
    // only for uart 0
    uart_enable_swap();

    uart_set_line_inverse(uart_num, UART_INVERSE_RXD | UART_INVERSE_TXD);

    uart_driver_install(uart_num, 256,0,0,NULL,0);

    uart_intr_config_t int_cfg = {
        .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA, // RXFIFO_FULL TXFIFO_EMPTY PARITY_ERR FRM_ERR RXFIFO_OVF RXFIFO_TOUT
        .rx_timeout_thresh = 0,
        .txfifo_empty_intr_thresh = 0,
        .rxfifo_full_thresh = 1
    };
    uart_intr_config(uart_num, &int_cfg);

    auto uartbus = new EbusBusUart(uart_num);

    bus = uartbus;

    auto dev = new EbusDeviceDebug(masterAddress, bus);
    uartbus->AddDevice(dev);

//    auto dev91 = new EbusDeviceVr91(0x30, bus);
//    uartbus->AddDevice(dev91);

    //auto dev70 = CreateVR70Device(0);
    //uartbus->AddDevice(dev70);

    // "MF=Vaillant;ID=V32;SW=0106;HW=6004"
    auto dev32 = CreateBridgeDevice(2, bus);
    uartbus->AddDevice(dev32);

    EbusBus *bus32 = dev32;
    // MF=Vaillant;ID=BAI00;SW=0518;HW=7401
    auto devBAI = CreateBAI(bus32);
    bus32->AddDevice(devBAI);


// cant use vr65 with combi
//    auto vr65 = CreateVR65Device(false, 2);
//    uartbus->AddDevice(vr65);

    uartbus->start();

    dev->monitor = initialise_ebusd(dev);

}

struct {
    struct arg_str *data;
    struct arg_end *end;
} ebus_data_args;

uint8_t fromHex(const char*h)
{
    uint8_t c1 = h[0] > '9' ? (h[0] & 7) + 9 : h[0] - '0';
    uint8_t c2 = h[1] > '9' ? (h[1] & 7) + 9 : h[1] - '0';
    return (c1<<4) | c2;
}

int ebus_data_func(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &ebus_data_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ebus_data_args.end, argv[0]);
        return 1;
    }

    auto data = ebus_data_args.data->sval[0];
    auto l = strlen(data);

    // ddCCccll
    if (l < 8 || l != 8+fromHex(data+6)*2) {
        printf("Error length\r\n");
        return 1;
    }

    auto cmd = new EbusMessageWriter();
    cmd->Write(masterAddress);

    for(int n = 0; n < l; n+=2)
    {
        cmd->Write(fromHex(data+n));
    }

    cmd->SetCRC();
    bus->QueueMessage(cmd);

    return 0;
}

void register_ebus_cmds()
{
    ebus_data_args.data = arg_str1(NULL,NULL,"<hex>","data");
    ebus_data_args.end = arg_end(1);
    const esp_console_cmd_t ebus_data_cmd = {
        .command = "ebus_data",
        .help = "Send Ebus data",
        .hint = NULL,
        .func = ebus_data_func,
        .argtable = &ebus_data_args
    };
    esp_console_cmd_register(&ebus_data_cmd);

    register_vr65_cmds();
    register_vr70_cmds();
}
