#ifndef GAZELLEMQ_SERVER_MESSAGE_HPP
#define GAZELLEMQ_SERVER_MESSAGE_HPP

namespace gazellemq::server {
    struct Message {
        std::string messageType;
        std::string content;
    };
}

#endif //GAZELLEMQ_SERVER_MESSAGE_HPP
