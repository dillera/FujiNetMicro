
#include "rs232Clock.h"

#include "../../include/debug.h"

rs232Clock platformClock;

std::optional<std::string> rs232Clock::fujidev_read_tz()
{
    return read_tz_from_payload();
}

