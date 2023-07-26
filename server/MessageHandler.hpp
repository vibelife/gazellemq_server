#ifndef GAZELLEMQ_MESSAGEHANDLER_HPP
#define GAZELLEMQ_MESSAGEHANDLER_HPP

#include "EventLoopObject.hpp"

namespace gazellemq::server {
    class MessageHandler : public EventLoopObject {
    public:
        virtual ~MessageHandler() = default;
    };
}

#endif //GAZELLEMQ_MESSAGEHANDLER_HPP
