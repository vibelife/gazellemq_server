#ifndef GAZELLEMQ_MESSAGEHANDLER_HPP
#define GAZELLEMQ_MESSAGEHANDLER_HPP

#include "EventLoopObject.hpp"

namespace gazellemq::server {
    class MessageHandler : public EventLoopObject {
    public:
        std::string clientName;

    public:
        virtual ~MessageHandler() = default;
        virtual void printHello() = 0;
    };
}

#endif //GAZELLEMQ_MESSAGEHANDLER_HPP
