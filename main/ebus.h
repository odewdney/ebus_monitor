#define SYN 0xaa
#define ESC 0xa9
#define ACK 0x00
#define NAK 0xff
#define BROADCAST_ADDR 0xfe

#define EBUS_MAX_PAYLOAD 16
#define EBUS_HEADER_SIZE 5
#define EBUS_CRC_SIZE 1

#ifdef __cplusplus
extern "C" {
#endif


void start_ebus_task();

uint8_t crc8v(const uint8_t *buf, int len);
bool IS_MASTER(uint8_t c);

#ifdef __cplusplus
}
#endif
