#ifndef GAZELLEMQ_SERVER_DATAPUSH_HPP
#define GAZELLEMQ_SERVER_DATAPUSH_HPP

namespace gazellemq::server {
    struct DataPush {
        int fd;
        size_t nbBytesSent{};
        std::string message;
    };
}

#endif //GAZELLEMQ_SERVER_DATAPUSH_HPP
