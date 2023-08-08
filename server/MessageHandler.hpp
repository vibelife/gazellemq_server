#ifndef GAZELLEMQ_MESSAGEHANDLER_HPP
#define GAZELLEMQ_MESSAGEHANDLER_HPP

#include "EventLoopObject.hpp"

namespace gazellemq::server {
    class MessageHandler : public EventLoopObject {
    public:
        std::string clientName;
        const int fd;
    protected:
        bool isZombie{};

    public:
        explicit MessageHandler(int fileDescriptor)
                :fd(fileDescriptor)
        {}

        /**
         * Returns true if this handler is about to be deleted
         * @return
         */
        [[nodiscard]] bool getIsZombie() const {
            return isZombie;
        }

        /**
         * This subscriber becomes inactive, then eventually deleted.
         */
        void markForRemoval() {
            if (!isZombie) {
                isZombie = true;
                clientName.append(" [Zombie]");
            }
        }

        virtual ~MessageHandler() = default;
        [[nodiscard]] virtual bool isSubscriber() const { return false; };
        virtual void printHello() const = 0;
    };
}

#endif //GAZELLEMQ_MESSAGEHANDLER_HPP
