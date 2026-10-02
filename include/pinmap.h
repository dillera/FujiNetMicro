/* FujiNet Hardware Pin Mapping */
#ifndef PINMAP_H
#define PINMAP_H

#ifdef ESP_PLATFORM
#include <hal/gpio_types.h>

#include "pinmap/rs232_rev0.h"
#include "pinmap/rs232_s3.h"
#include "pinmap/fujiversal-rs232.h"

#include "pinmap_defaults.h"

#endif // ESP_PLATFORM

#endif /* PINMAP_H */
