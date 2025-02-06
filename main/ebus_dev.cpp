#include "stdint.h"
#include "stdio.h"

#include "ebus.h"

#include <vector>
#include <algorithm>
#include <cstring>

#include "ebus_dev.h"

#include "esp_log.h"

EbusMessage::EbusMessage(uint8_t src, uint8_t dst, uint16_t cmd)
{
    buffer[0] = src;
    buffer[1] = dst;
    buffer[2] = cmd >> 8;
    buffer[3] = cmd & 0xff;
    buffer[4] = 0;
}

EbusMessage::EbusMessage(uint8_t const *buf)
    : EbusBuffer(buf)
{
}

int EbusMessage::GetMessageLength()
{
    int len = EBUS_HEADER_SIZE + buffer[4];
    buffer[len] = crc8v(buffer, len);
    return len + EBUS_CRC_SIZE;
}

void EbusMessage::print() const
{
    int l = buffer[4];
    printf("Req: %02x %02x %04x (%d)", GetSource(), GetDest(), GetCmd(), l);
    if ( l > 0) {
        if (l > EBUS_MAX_PAYLOAD ) l = EBUS_MAX_PAYLOAD;
        printf(" Data:");
        for(int n = 0; n < l; n++)
            printf(" %02x", buffer[5+n]);
    }
    printf(" (%02x)\r\n", buffer[5+l]);
}

bool EbusMessageWriter::Write(uint8_t c)
{
    if ( len < 5 ) {
        buffer[len++] = c;
        return false;
    }
    int l = buffer[4];
    if (l > EBUS_MAX_PAYLOAD)
        l = EBUS_MAX_PAYLOAD;
    if (len >= (6+l))
        return true; // overflow
    buffer[len++] = c;
    if (len == (6+l))
        return true; // end

    return false;
}

EbusResponse::EbusResponse(uint8_t const *buf)
    : EbusBuffer(buf)
{
}

EbusMessage::EbusMessage(EbusMessage const &msg)
    : EbusBuffer( msg.buffer )
{
}


bool EbusResponseWriter::Write(uint8_t c)
{
    if ( len < 1 ) {
        buffer[len++] = c;
        return false;
    }
    int l = buffer[0];
    if (l > EBUS_MAX_PAYLOAD)
        l = EBUS_MAX_PAYLOAD;
    if (len >= (2+l))
        return true; // overflow
    buffer[len++] = c;
    if (len == (2+l))
        return true; // end

    return false;
}

void EbusResponse::print() const
{
    int l = buffer[0];
    printf("Res: (%d)", l);
    if ( l > 0) {
        printf(" Data:");
        if (l > EBUS_MAX_PAYLOAD ) l = EBUS_MAX_PAYLOAD;
        for(int n = 0; n < l; n++)
            printf(" %02x", buffer[1+n]);
    }
    printf(" (%02x)\r\n", buffer[1+l]);
}

EbusDevice::EbusDevice(uint8_t addr, const char*n)
{
    slaveAddress = addr;
    name = n;
}

template<std::size_t N, std::size_t M>
void EbusDevice::WriteID(EbusBuffer<N,M> &buffer, uint8_t manu, const char*name, uint16_t sw, uint16_t hw)
{
    buffer.AddPayload(manu);
    buffer.AddPayload(name, 5);
    buffer.AddPayloadVersion(sw);
    buffer.AddPayloadVersion(hw);
}

template void EbusDevice::WriteID(EbusBuffer<22,4> &buffer, uint8_t manu, const char*name, uint16_t sw, uint16_t hw);
template void EbusDevice::WriteID(EbusBuffer<18,0> &buffer, uint8_t manu, const char*name, uint16_t sw, uint16_t hw);

void EbusDevice::print()
{
    printf("Device: %s id:%02x\r\n", name, slaveAddress);
}

void EbusDevice::ProcessBroadcastMessage(EbusMessage const &msg)
{
}

bool EbusDevice::ProcessResponse(EbusMessage const &msg, EbusResponse const &response)
{
    return false;
}

const char*EbusBus::TAG = "EBUS";

void EbusBus::AddDevice(EbusDevice *dev)
{
    ESP_LOGI("EBUS", "Added device %02x %s", dev->GetSlaveAddress(), dev->GetName());
    devices.push_back(dev);
}

void EbusBus::RemoveDevice(EbusDevice *dev)
{
    auto pos = std::find(devices.begin(), devices.end(), dev);
    if(pos != devices.end())
        devices.erase(pos);
}

EbusDevice *EbusBus::GetDevice(uint8_t dev)
{
    for(auto pos = devices.begin(); pos != devices.end(); pos++ ) {
        auto device = *pos;
        if ( device->GetSlaveAddress() == dev) {
            return device;
        }
    }
    return nullptr;
}

void EbusBus::ProcessMessage(EbusMessage const &msg)
{
    if (msg.GetDest() == BROADCAST_ADDR)
        ProcessBroadcastMessage(msg);
    else
        ProcessDeviceMessage(msg);
}

void EbusBus::ProcessDeviceMessage(EbusMessage const &msg)
{
    auto dst = msg.GetDest();
    if (IS_MASTER(dst))
        dst += 5; // get slave address for target
    ESP_LOGV(TAG, "Looking for %02x", dst);
    auto device = GetDevice(dst);
    if (device) {
        ESP_LOGV(TAG, "Found dev %s", device->GetName());
        bool success = false;
        EbusResponse *response = nullptr;

        if ( msg.IsValidCRC() ) {
            success = device->ProcessSlaveMessage(msg, &response);
        } else {
            ESP_LOGE(TAG, "Bad CRC");
        }

        if (success) {
            SendACK();
            if (response) {
                response->SetCRC();
                SendResponse(*response);
            }
        } else {
            SendNAK();
        }
        if ( response )
            delete response;
    }
}

bool EbusBus::ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response)
{
    bool success = false;
    auto dst = msg.GetDest();
    if (IS_MASTER(dst))
        dst += 5; // get slave address for target
    ESP_LOGV(TAG, "Looking for %02x", dst);
    auto device = GetDevice(dst);
    if (device) {
        ESP_LOGV(TAG, "Found dev %s", device->GetName());

        if ( msg.IsValidCRC() ) {
            success = device->ProcessSlaveMessage(msg, response);
        } else {
            ESP_LOGE(TAG, "Bad CRC");
        }
    }
    return success;
}

void EbusBus::ProcessBroadcastMessage(EbusMessage const &msg)
{
    for(auto device : devices) {
        device->ProcessBroadcastMessage(msg);
    }
}

void EbusBus::ProcessResponse(EbusMessage const &msg, EbusResponse const &response)
{
    auto dst = msg.GetSource();
    if (IS_MASTER(dst))
        dst += 5; // get slave address for target
    auto device = GetDevice(dst);
    if (device) {
        auto success = false;
        
        if (msg.IsValidCRC() && response.IsValidCRC()) {
            success = device->ProcessResponse(msg, response);
        }

        if (success)
            SendACK();
        else
            SendNAK();
    }
}

void EbusBusData::SendChar(uint8_t c)
{
    SendData(&c,1);
}

void EbusBusData::SendACK()
{
    SendChar(ACK);
}

void EbusBusData::SendNAK()
{
    SendChar(NAK);
}

void EbusBusData::SendResponse(EbusResponse const &response)
{
    SendData(response.GetBuffer(), response.GetBufferLength() );
}

