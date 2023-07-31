#ifndef GAZELLEMQ_CONSTS_HPP
#define GAZELLEMQ_CONSTS_HPP

#include <thread>
#include <netdb.h>

namespace gazellemq::server {
    static constexpr addrinfo GAI_HINTS{0, AF_INET, SOCK_STREAM, 0, 0, nullptr, nullptr, nullptr};
    static constexpr auto MAX_READ_BUF = 256;
    static constexpr auto BIG_READ_BUF = 8192;
    static constexpr auto LAST_MSG_BUF_SIZE = 4096;
    static constexpr auto DEFAULT_OUT_QUEUE_DEPTH = 64;
    static constexpr auto DEFAULT_IN_QUEUE_DEPTH = 8;
    static constexpr auto TIMEOUT = -62;
    static inline const auto DEFAULT_NB_THREADS = std::thread::hardware_concurrency();
}

#endif //GAZELLEMQ_CONSTS_HPP
