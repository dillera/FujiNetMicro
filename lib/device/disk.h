#ifndef DEVICE_DISK_H
#define DEVICE_DISK_H

enum disk_access_flags_t {
    DISK_ACCESS_MODE_INVALID = 0x00,
    DISK_ACCESS_MODE_READ    = 0x01,
    DISK_ACCESS_MODE_WRITE   = 0x02,
    DISK_ACCESS_MODE_MOUNTED = 0x40,
};

#include "rs232/disk.h"
#define DISK_DEVICE rs232Disk

#endif // DEVICE_DISK_H
