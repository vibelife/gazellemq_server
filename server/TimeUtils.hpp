#ifndef TIMEUTILS_HPP
#define TIMEUTILS_HPP

#include <ctime>
#include <chrono>

/**
 * Returns the current unix timestamp
 * @return
 */
static unsigned long nowToLong() {
    return static_cast<long>(static_cast<std::chrono::duration<double, std::milli>>(std::chrono::high_resolution_clock::now().
        time_since_epoch()).count());
}

#endif //TIMEUTILS_HPP
