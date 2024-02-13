#ifndef GAZELLEMQ_MESSAGEHANDLER_HPP
#define GAZELLEMQ_MESSAGEHANDLER_HPP

#include "EventLoopObject.hpp"

namespace gazellemq::server {
    class MessageHandler : public EventLoopObject {
    public:
        std::string clientName;
        int fd{};
    protected:
        bool isZombie{};
        bool mustDisconnect{};

    public:
        explicit MessageHandler(int fileDescriptor)
                :fd(fileDescriptor)
        {}

        MessageHandler(int fileDescriptor, std::string &&clientName)
                :fd(fileDescriptor), clientName(std::move(clientName))
        {}

        /**
         * Returns true if this handler is about to be deleted
         * @return
         */
        [[nodiscard]] bool getIsZombie() const {
            return isZombie;
        }

        /**
         * Returns true if this handler needs to disconnect
         * @return
         */
        [[nodiscard]] bool getMustDisconnect() const {
            return mustDisconnect;
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

        void forceDisconnect() {
            if (!mustDisconnect) {
                mustDisconnect = true;
            }
        }

        virtual ~MessageHandler() = default;
        [[nodiscard]] virtual bool isSubscriber() const { return false; };
        virtual void printHello() const = 0;
    };
}

#endif //GAZELLEMQ_MESSAGEHANDLER_HPP
