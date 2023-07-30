#ifndef GAZELLEMQ_SERVER_PUSHSERVICE_HPP
#define GAZELLEMQ_SERVER_PUSHSERVICE_HPP

#include <condition_variable>
#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "Message.hpp"
#include "MessageSubscriber.hpp"
#include "Consts.hpp"


namespace gazellemq::server {
    class PushService {
    private:
        std::vector<MessageSubscriber*> subscribers;

    public:
        /**
         * Pushes the message out to subscribers.
         * @param ring
         * @param messageType
         * @param messageContent
         */
        void push(struct io_uring *ring, std::string&& messageType, std::string&& messageContent) {
            std::string msg;
            msg.reserve(messageType.size() + messageContent.size() + 16);

            msg.append(messageType);
            msg.push_back('|');
            msg.append(std::to_string(messageContent.size()));
            msg.push_back('|');
            msg.append(messageContent);

            for (auto* subscriber : subscribers) {
                if (subscriber->isSubscribed(messageType)) {
                    auto* message = new Message{};
                    message->content.reserve(msg.size());
                    message->content.append(msg);
                    subscriber->push(ring, message);
                }
            }
        }

        /**
         * Registers a subscriber so it can receive messages
         * @param subscriber
         */
        void registerSubscriber(MessageSubscriber* subscriber) {
            subscribers.emplace_back(subscriber);
        }
    };

    inline PushService _mq{};

    static PushService& getPushService() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_PUSHSERVICE_HPP
