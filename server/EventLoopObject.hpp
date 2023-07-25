#ifndef GAZELLEMQ_EVENTLOOPOBJECT_HPP
#define GAZELLEMQ_EVENTLOOPOBJECT_HPP

#include "Enums.hpp"

namespace gazellemq::server {
    class EventLoopObject {
    public:
        Enums::Event event;
    public:
        virtual void handleEvent(struct io_uring *ring) = 0;
    };
}

#endif //GAZELLEMQ_EVENTLOOPOBJECT_HPP
