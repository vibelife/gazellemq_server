#ifndef SUBSCRIBERHANDLER_HPP
#define SUBSCRIBERHANDLER_HPP
#include <list>
#include <vector>

#include "../MessageBatch.hpp"
#include "../PubSubHandler.hpp"
#include "../StringUtils.hpp"

namespace gazellemq::server {
    class TCPSubscriberHandler : public PubSubHandler {
    protected:
        std::vector<std::string> subscriptions;
        std::string subscriptionsBuffer;
        std::list<MessageBatch> pendingItems;
        MessageBatch currentItem{};
        bool isNew{true};
        bool isDisconnected{false};
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

        [[nodiscard]] bool getIsDisconnected() const override {
            return isDisconnected;
        }

        void markForRemoval() override {
            isDisconnected = true;
        }

        [[nodiscard]] bool getIsNew() const override {
            return isNew;
        }

        void setIsNew(bool b) override {
            isNew = b;
        }
    protected:
        void onDisconnected (int res) override {
            std::cout << "Subscriber disconnected [" << clientName << "]\n";
            markForRemoval();
        }
    public:
        /**
         * Returns true if the passed in string is subscribed to by this subscriber.
         * @param messageType
         * @return
         */
        [[nodiscard]] bool isSubscribed(std::string_view messageType) const {
            return std::any_of(subscriptions.begin(), subscriptions.end(), [messageType](std::string const& o) {
                return o.starts_with(messageType);
            });
        }

        void afterSendAckComplete(struct io_uring *ring) override {
            memset(readBuffer, 0, MAX_READ_BUF);
            // beginReceiveSubscriptions(ring);
            event = Enums::Event_Ready;

            // look for pending subscriptions
            for (auto const& subscription : serverContext->takePendingSubscriptions(clientName)) {
                addSubscriptions(subscription);
            }
        }

        void handle(struct io_uring *ring, int res) override {
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
         * @param subscriptionsCsv
         */
        void addSubscriptions(std::string const& subscriptionsCsv) {
            std::vector<std::string> subscriptionValues;
            gazellemq::utils::split(std::string(subscriptionsCsv), subscriptionValues);

            // add each subscription that does not already exist
            for (std::string const& subscriptionValue : subscriptionValues) {
                if (std::ranges::none_of(subscriptions, [subscriptionValue](std::string const& o) {
                    return o == subscriptionValue;
                })) {
                    std::cout << "[" << clientName << "] adding subscription | " << subscriptionValue << std::endl << std::flush;
                    subscriptions.push_back(subscriptionValue);
                }
            }
        }


        /**
         * Sends the data to the subscriber, or queues it to be sent later.
         * @param ring
         * @param batch
         */
        void pushMessageBatch(struct io_uring *ring, MessageBatch const& batch) {
            if (!currentItem.hasContent()) {
                currentItem.copy(batch);
                sendCurrentMessage(ring);
            } else {
                pendingItems.push_back(batch.copy());
            }
        }

        void sendNextPendingMessageBatch(struct io_uring *ring) {
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
        void sendCurrentMessage(struct io_uring *ring) {
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
        void onSendCurrentMessageComplete(struct io_uring *ring, int res) {
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
