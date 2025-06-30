#ifndef SUBSCRIBERHANDLER_HPP
#define SUBSCRIBERHANDLER_HPP
#include <list>
#include <vector>

#include "../MessageBatch.hpp"
#include "../PubSubHandler.hpp"
#include "../StringUtils.hpp"

namespace gazellemq::server {
    class TCPSubscriberHandler : public PubSubHandler {
    private:
        struct SubscriptionData {
            unsigned long timeout{};
            unsigned long lastAction{};
            bool timeoutExpired{};
        };
    protected:
        std::unordered_map<std::string, SubscriptionData> subscriptions;
        std::string buffer;
        std::list<MessageBatch> pendingItems;
        MessageBatch currentItem{};
        bool isNew{true};
    public:
        TCPSubscriberHandler(int res, ServerContext* serverContext)
            : PubSubHandler(res, serverContext)
        {}

        void setServerContext(ServerContext* serverContext) {
            this->serverContext = serverContext;
        }

        void printHello() override {
            std::cout << clientName << " | a subscriber has connected" << std::endl << std::flush;
        }

        [[nodiscard]] bool getIsNew() const override {
            return isNew;
        }

        void setIsNew(bool b) override {
            isNew = b;
        }
    protected:
        void updateLastAction(std::string const& messageType) {
            if (subscriptions.contains(messageType)) {
                subscriptions.at(messageType).lastAction = nowToLong();
            }
        }

        void onDisconnected (int res) override {
            std::cout << "Subscriber disconnected [" << clientName << "]\n";
            setDisconnected();
        }
    public:
        /**
         * Checks if any subscription has timed out
         */
        void handleTimeout() {
            unsigned long now = nowToLong();
            bool anyTimedOut = false;
            std::ranges::for_each(subscriptions, [&](auto& item) {
                if (item.second.timeout > 0 && ((now - item.second.lastAction) > item.second.timeout)) {
                    std::cout << "[" << clientName << "] subscription timeout | " << item.second.timeout << "ms | " << item.first << std::endl;
                    item.second.timeoutExpired = true;
                    anyTimedOut = true;
                }
            });

            if (anyTimedOut) {
                std::erase_if(subscriptions, [&](auto& item) {
                    return item.second.timeoutExpired;
                });
            }
        }

        /**
         * Returns true if the passed in string is subscribed to by this subscriber.
         * @param messageType
         * @return
         */
        [[nodiscard]] bool isSubscribed(std::string_view messageType) const {
            return std::ranges::any_of(subscriptions, [messageType](auto const& o) {
                return !o.second.timeoutExpired && o.first.starts_with(messageType);
            });
        }

        void afterSendAckComplete(io_uring *ring) override {
            memset(readBuffer, 0, MAX_READ_BUF);
            buffer.clear();

            event = Enums::Event_Ready;

            // look for pending subscriptions
            for (auto const& subscription : serverContext->takePendingSubscriptions(clientName)) {
                addSubscriptions(subscription.timeoutMs, subscription.subscription);
            }
        }

        void handle(io_uring *ring, int res) override {
            if (getIsDisconnected()) return;

            switch (event) {
                case Enums::Event_SendData:
                    onSendCurrentMessageComplete(ring, res);
                    break;
                default:
                    break;
            }
        }

        /**
         * Adds subscriptions to the list, but only ones that do not already exist
         * @param timeoutMs
         * @param subscriptionsCsv
         */
        void addSubscriptions(unsigned long timeoutMs, std::string const& subscriptionsCsv) {
            std::vector<std::string> subscriptionValues;
            utils::split(std::string(subscriptionsCsv), subscriptionValues);

            // add each subscription that does not already exist
            for (std::string const& subscriptionValue : subscriptionValues) {
                if (std::ranges::none_of(subscriptions, [subscriptionValue](auto const& o) {
                    return o.first == subscriptionValue;
                })) {
                    std::cout << "[" << clientName << "] adding subscription | " << subscriptionValue << std::endl << std::flush;
                    subscriptions[subscriptionValue] = SubscriptionData{timeoutMs, nowToLong()};
                }
            }
        }

        /**
         * Sends the data to the subscriber, or queues it to be sent later.
         * @param ring
         * @param batch
         */
        void pushMessageBatch(io_uring *ring, MessageBatch const& batch) {
            updateLastAction(batch.getMessageType());

            if (!currentItem.hasContent()) {
                currentItem.copy(batch);
                sendCurrentMessage(ring);
            } else {
                pendingItems.push_back(batch.copy());
            }
        }

        /**
         * Sends the pending batch of messages
         * @param ring
         */
        void sendNextPendingMessageBatch(io_uring *ring) {
            currentItem = std::move(pendingItems.front());
            pendingItems.pop_front();

            if (currentItem.hasContent()) {
                currentItem.setBusy();
                sendCurrentMessage(ring);
            }
        }

        /**
         * Sends the remaining bytes to the endpoint
         * @param ring
         */
        void sendCurrentMessage(io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, currentItem.getBufferRemaining(), currentItem.getBufferLength(), 0);

            event = Enums::Event_SendData;
            io_uring_sqe_set_data(sqe, this);
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done sending the bytes
         * @param ring
         * @param res
         * @return
         */
        void onSendCurrentMessageComplete(io_uring *ring, const int res) {
            if (res == 0) {
                // do nothing here
                std::cout << "possible disconnected subscriber\n";
            } else if (res > -1) {
                currentItem.advance(res);
                if (currentItem.getIsDone()) {
                    currentItem.clearForNextMessage();
                    // To get here means we've sent all the data

                    // collect the next messages
                    if (!pendingItems.empty()) {
                        sendNextPendingMessageBatch(ring);
                    } else {
                        event = Enums::Event_Ready;
                    }
                } else {
                    sendCurrentMessage(ring);
                }
            } else {
                printError("onSendCurrentMessageComplete", res);
                beginDisconnect(ring);
            }
        }
    };
}

#endif //SUBSCRIBERHANDLER_HPP
