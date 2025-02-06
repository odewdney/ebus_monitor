

class EbusDeviceBase1 : public EbusDevice
{
protected:
    uint8_t manu;
    uint16_t sw,hw;
    EbusDeviceBase1(uint8_t slaveAddr, uint8_t m, const char *n,uint16_t s, uint16_t h)
        :EbusDevice(slaveAddr, n)
    {
        manu = m;
        sw=s;
        hw=h;
    }

    virtual bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response);

};

class EbusDeviceBase : public EbusDeviceBase1
{
protected:
    EbusBus *bus;
    uint8_t masterAddress;
    TimerHandle_t bcastTimer;

    virtual bool ProcessTimer(int cnt);

    int cnt = 0;
    static void processMasterTimer(TimerHandle_t handle);

public:
    EbusDeviceBase(uint8_t addr, uint8_t m, const char *n,uint16_t s, uint16_t h,EbusBus *b)
        :EbusDeviceBase1(addr+5, m,n,s,h)
    {
        masterAddress = addr;
        bus = b;
    }

    void start();
};

class EbusDeviceBridgeBase : public EbusDeviceBase, public EbusBus
{
protected:
    EbusDeviceBridgeBase(uint8_t addr, uint8_t m, const char *n,uint16_t s, uint16_t h,EbusBus *b)
        :EbusDeviceBase(addr,m,n,s,h,b)
    {}
};

EbusDevice *CreateVR70Device(uint8_t index);
void register_vr70_cmds();

EbusDevice *CreateVR65Device(bool isVr66 = false, uint8_t sw = 0);
void register_vr65_cmds();

EbusDeviceBridgeBase *CreateBridgeDevice(uint8_t masterAddr, EbusBus *bus);
EbusDevice *CreateBAI(EbusBus *bus);


EbusDevice *CreateVR91Device(uint8_t sw, EbusBus *bus);
void register_bai_cmds();

extern "C"
{
void register_ebus_cmds();
}

