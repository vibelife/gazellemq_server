#ifndef GAZELLEMQ_EVENTLOOPOBJECT_HPP
#define GAZELLEMQ_EVENTLOOPOBJECT_HPP

#include <cstring>
#include <cstdio>
#include "Enums.hpp"

namespace gazellemq::server {
    class EventLoopObject {
    public:
        Enums::Event event{Enums::Event::Event_NotSet};
    public:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        virtual void handleEvent(struct io_uring *ring, int res) = 0;
    };
}

#endif //GAZELLEMQ_EVENTLOOPOBJECT_HPP
