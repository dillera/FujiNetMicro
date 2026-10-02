#ifndef CMDFRAME_H
#define CMDFRAME_H

#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "global_types.h"

typedef struct
{
    fujiDeviceID_t device;
    fujiCommandID_t comnd;
    union {
        struct {
            uint8_t aux1;
            uint8_t aux2;
            uint8_t aux3;
            uint8_t aux4;
        };
        struct {
            uint16_t aux12;
            uint16_t aux34;
        };
        uint32_t aux;
    };
    uint8_t cksum;
} __attribute__((packed)) cmdFrame_t;
static_assert(sizeof(cmdFrame_t) == 7, "cmdFrame_t must be 7 bytes");

#endif /* CMDFRAME_H */
