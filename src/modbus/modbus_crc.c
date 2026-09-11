#include "modbus/modbus_crc.h"

unsigned short modbus_crc16(const unsigned char *data, unsigned int length)
{
    unsigned short crc = 0xffffU;
    unsigned int i;
    unsigned int bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? (unsigned short)((crc >> 1) ^ 0xa001U)
                             : (unsigned short)(crc >> 1);
    }
    return crc;
}
