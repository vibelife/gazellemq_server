#ifndef GAZELLEMQ_MESSAGEHANDLER_HPP
#define GAZELLEMQ_MESSAGEHANDLER_HPP

#include "EventLoopObject.hpp"

namespace gazellemq::server {
    class MessageHandler : public EventLoopObject {
    public:
        std::string clientName;
        const int fd;

    public:
        explicit MessageHandler(int fileDescriptor)
                :fd(fileDescriptor)
        {}

        virtual ~MessageHandler() = default;
        virtual void printHello() const = 0;
    };
}

#endif //GAZELLEMQ_MESSAGEHANDLER_HPP
