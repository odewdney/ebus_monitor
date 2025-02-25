#include <math.h>
#include <vector>
#include <limits>

class Ebus
{
public:
    static const uint8_t BYTE_REPLACEMENT = 0xff;
    static const uint16_t WORD_REPLACEMENT = 0xffff;
    static const int16_t SWORD_REPLACEMENT = 0x8000;
};

template<std::size_t N, std::size_t M>
class EbusBuffer
{
protected:
    uint8_t buffer[N];
    EbusBuffer(const uint8_t *buf)
    {
        int len = M+buf[M]+2;
        if (len>N) len = N;
        memcpy(buffer, buf, len);
    }

    EbusBuffer() {buffer[M] = 0;}
public:

    void AddPayload(uint8_t c)
    {
        buffer[M+(++buffer[M])] = c;
    }

    void AddPayloadBCD(uint8_t c)
    {
        c = ((c/10)<<4) | (c%10);
        buffer[M+(++buffer[M])] = c;
    }

    void AddPayload(const char *c, int len)
    {
        while(len--)
            buffer[M+(++buffer[M])] = *c++;
    }

    void AddPayloadWord(uint16_t d)
    {
        buffer[M+(++buffer[M])] = d & 0xff;
        buffer[M+(++buffer[M])] = d>>8;
    }

    void AddPayloadSWord(int16_t d)
    {
        AddPayloadWord((uint16_t)d);
    }

    void AddPayloadVersion(uint16_t d)
    {
        buffer[M+(++buffer[M])] = d>>8;
        buffer[M+(++buffer[M])] = d & 0xff;
    }

    void AddPayloadData1c(float f)
    {
        buffer[M+(++buffer[M])] = (int)(f*2);
    }

    void AddPayloadData2b(float f)
    {
        int d = (int)(f*256);
        AddPayloadWord((uint16_t)d);
    }

    void AddPayloadData2c(float f)
    {
        int d = (int)(f*16);
        AddPayloadWord((uint16_t)d);
    }

    void AddPayloadDWord(uint32_t d)
    {
        buffer[M+(++buffer[M])] = d & 0xff;
        d >>= 8;
        buffer[M+(++buffer[M])] = d & 0xff;
        d >>= 8;
        buffer[M+(++buffer[M])] = d & 0xff;
        d >>= 8;
        buffer[M+(++buffer[M])] = d & 0xff;
        d >>= 8;
    }

    void AddPayloadEXP(float f)
    {
        if (std::isnan(f))
            AddPayloadDWord(0x7fffffff);
        else
            AddPayloadDWord(*(uint32_t*)&f);
    }

    float ReadPayloadData1c(uint8_t offset) const
    {
        auto ret = buffer[1+M+offset];
        if ( ret == 0xff)
            return std::numeric_limits<float>::quiet_NaN();
        return ret / 2.0f;
    }

    int16_t ReadPayloadWord(uint8_t offset) const
    {
        return buffer[1+M+offset] | (buffer[2+M+offset] << 8);
    }

    int16_t ReadPayloadSWord(uint8_t offset) const
    {
        return (int16_t)ReadPayloadWord(offset);
    }

    float ReadPayloadData2b(uint8_t offset) const
    {
        int16_t ret = ReadPayloadSWord(offset);
        if (ret == 0x8000)
            return std::numeric_limits<float>::quiet_NaN();

        return  ret / 256.0f;
    }

    float ReadPayloadData2c(uint8_t offset) const
    {
        int16_t ret = ReadPayloadSWord(offset); 
        if (ret == 0x8000)
            return std::numeric_limits<float>::quiet_NaN();
        return ret / 16.0f;
    }

    int ReadPayloadBCD(uint8_t offset) const
    {
        auto c = buffer[1+M+offset];
        return 10*(c>>4)+(c&0xf);
    }

    int GetPayloadLength() const { return buffer[M]; }
    const uint8_t *GetPayload() const { return &buffer[M+1]; }
    const uint8_t *GetBuffer() const {return buffer;}
    int GetBufferLength() const { return M + buffer[M] + 2; }

    void SetCRC()
    {
        int len = M + buffer[M];
        if ( len > N)
            return;
        len++;
        auto crc = crc8v(buffer, len);
        buffer[len] = crc;
    }

    bool IsValidCRC() const
    {
        if (buffer[M] > EBUS_MAX_PAYLOAD)
            return false;
        int len = M + buffer[M];
        len++;
        auto crc = crc8v(buffer,len);
        return crc == buffer[len];
    }
};

class EbusMessage : public EbusBuffer<EBUS_HEADER_SIZE + EBUS_MAX_PAYLOAD + EBUS_CRC_SIZE, 4>
{
//    uint8_t buffer[EBUS_HEADER_SIZE + EBUS_MAX_PAYLOAD + EBUS_CRC_SIZE];
protected:
    EbusMessage() {}
public:
    EbusMessage(uint8_t src, uint8_t dst, uint16_t cmd);
    EbusMessage(uint8_t const *buf);
    EbusMessage(EbusMessage const &msg);

    int GetMessageLength(); // sets CRC

    uint8_t GetSource() const { return buffer[0]; }
    uint8_t GetDest() const { return buffer[1]; }
    uint16_t GetCmd() const { return (buffer[2]<<8)|buffer[3]; }

    void print() const;
};

class EbusMessageWriter : public EbusMessage
{
    std::size_t len = 0;
public:
    bool Write(uint8_t c);
    void Reset() { len = 0;}
    bool IsEmpty() { return len == 0; }
};

class EbusResponse : public EbusBuffer<1 + EBUS_MAX_PAYLOAD + EBUS_CRC_SIZE,0>
{
public:
    EbusResponse() {}
    EbusResponse(uint8_t const *buf);

    void print() const;
};

class EbusResponseWriter : public EbusResponse
{
    std::size_t len = 0;
public:
    bool Write(uint8_t c);
    void Reset() { len = 0; buffer[0] = 0;}
    bool IsEmpty() const { return len == 0; }
    bool IsFull() const {return len == (buffer[0]+2); }
    int GetWrittenLen() const {return len;}
};

class EbusSender
{
public:
    virtual void Send(EbusMessage const &msg) = 0;
};

class EbusMonitor
{
public:
    virtual void NotifyBroadcast(EbusMessage const &msg) = 0;
    virtual void Notify(EbusMessage const &msg, EbusResponse const &response) = 0;
};

class EbusDevice
{
protected:
    uint8_t slaveAddress;
    const char *name;

    EbusDevice(uint8_t addr, const char*name);

    template<std::size_t N, std::size_t M>
    static void WriteID(EbusBuffer<N,M> &buffer, uint8_t manu, const char*name, uint16_t sw, uint16_t hw);

public:
    virtual bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response) = 0;
    virtual void ProcessBroadcastMessage(EbusMessage const &msg);
    virtual bool ProcessResponse(EbusMessage const &msg, EbusResponse const &response);

    uint8_t GetSlaveAddress() const { return slaveAddress; }
    const char *GetName() const { return name; }

    virtual void print();
    virtual void start() {}
};

class EbusBus
{
protected:
    static const char *TAG;

    std::vector<EbusDevice*> devices;

    virtual void SendACK() = 0;
    virtual void SendNAK() = 0;
    virtual void SendResponse(EbusResponse const &response) = 0;

    bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response);
    void ProcessDeviceMessage(EbusMessage const &msg);
    void ProcessBroadcastMessage(EbusMessage const &msg);
    void ProcessMessage(EbusMessage const &msg);

    virtual void ProcessResponse(EbusMessage const &msg, EbusResponse const &response);
public:
    void AddDevice(EbusDevice *dev);
    void RemoveDevice(EbusDevice *dev);
    EbusDevice *GetDevice(uint8_t id);

    virtual void QueueMessage(EbusMessage const *msg) =0;
};

class EbusBusData : public EbusBus
{
protected:
    virtual void SendData(const uint8_t*data, int len) = 0;
    void SendChar(uint8_t c);
    virtual void SendACK();
    virtual void SendNAK();
    virtual void SendResponse(EbusResponse const &response);
};



EbusMonitor *initialise_ebusd(EbusSender *sender);
EbusMonitor *initialise_mqtt(EbusSender *sender);
