#ifndef PUBSUBHANDLER_HPP
#define PUBSUBHANDLER_HPP

#include <cstring>
#include <iostream>
#include <sys/epoll.h>

#include "Enums.hpp"
#include "BaseObject.hpp"
#include "Consts.hpp"
#include "ServerContext.hpp"

namespace gazellemq::server {
    class PubSubHandler : public BaseObject {
    protected:
        static constexpr auto NB_INTENT_CHARS = 2;

        int fd;
        char readBuffer[8192]{};
        std::string intent{};
        Enums::Event event{Enums::Event::Event_NotSet};
        std::string clientName{};
        ServerContext* serverContext{nullptr};
    public:
        explicit PubSubHandler(int res, ServerContext* serverContext)
                :fd(res), serverContext(serverContext)
        {}

        ~PubSubHandler() override = default;

        PubSubHandler(PubSubHandler&& other) noexcept : fd(other.fd) {
            swap(std::move(other));
        }

        PubSubHandler& operator=(PubSubHandler&& other) noexcept {
            swap(std::move(other));
            return *this;
        }

        virtual void printHello() = 0;
    private:
        void swap(PubSubHandler&& other) {
            std::swap(this->fd, other.fd);
            std::swap(this->id, other.id);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->intent, other.intent);
            std::swap(this->event, other.event);
            std::swap(this->clientName, other.clientName);
        }
    public:
        [[nodiscard]] std::string getClientName() const {
            return clientName;
        }

        [[nodiscard]] virtual bool getIsDisconnected() const = 0;
        virtual void markForRemoval() = 0;

        [[nodiscard]] virtual bool getIsNew() const = 0;
        virtual void setIsNew(bool) = 0;

        [[nodiscard]] bool getIsIdle() const {
            return event == Enums::Event_Ready || event == Enums::Event_NotSet || event == Enums::Event_Disconnected;
        }

        void handleEvent(struct io_uring *ring, int res) override {
            if (this->getIsDisconnected()) return;

            switch (event) {
                case Enums::Event::Event_NotSet:
                    beginMakeNonblockingSocket(ring, res);
                    break;
                case Enums::Event::Event_SetNonblockingPublisher:
                    onMakeNonblockingSocketComplete(ring, res);
                    break;
                case Enums::Event::Event_ReceiveIntent:
                    onIntentReceived(ring, res);
                    break;
                case Enums::Event::Event_ReceiveName:
                    onReceiveNameComplete(ring, res);
                    break;
                case Enums::Event::Event_SendAck:
                    onSendAckComplete(ring, res);
                    break;
                case Enums::Event::Event_Disconnected:
                    onDisconnected(res);
                    break;
                default:
                    handle(ring, res);
                    break;
            }
        }
    protected:
        virtual void handle(struct io_uring* ring, int res) = 0;

        /**
         * Error handler
         * @param msg
         * @param err
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Disconnects from the server
         * @param ring
         */
        void beginDisconnect(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_close(sqe, fd);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_Disconnected;
            io_uring_submit(ring);
        }

        virtual void onDisconnected (int res) {
            std::cout << "Client disconnected\n";
            markForRemoval();
        }

        /**
         * Sets the client connection to non blocking
         * @param ring
         * @param epfd
         */
        void beginMakeNonblockingSocket(struct io_uring* ring, int epfd) {
            setIsNew(false);

            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.fd = fd;
            io_uring_prep_epoll_ctl(sqe, epfd, fd, EPOLL_CTL_ADD, &ev);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_SetNonblockingPublisher;
            io_uring_submit(ring);
        }

        /**
         * This client must now indicate whether it is a publisher or subscriber
         * @param ring
         * @param res
         */
        virtual void onMakeNonblockingSocketComplete(struct io_uring* ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {

                // client must communicate with server
                this->beginReceiveIntent(ring);
            }
        }

        /**
         * Wait for the client to pushToSubscribers indicate whether it is a publisher or subscriber
         * @param ring
         */
        void beginReceiveIntent(struct io_uring* ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, 2, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_ReceiveIntent;
            io_uring_submit(ring);
        }

        /**
         * Intent data received from the client
         * @param ring
         * @param res
         */
        void onIntentReceived(struct io_uring *ring, int res) {
            if (res == 0) {
                // no data
            } else if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {
                intent.append(readBuffer, res);
                if (intent.size() < NB_INTENT_CHARS) {
                    beginReceiveIntent(ring);
                } else {
                    // now receive the name from the client
                    memset(readBuffer, 0, MAX_READ_BUF);
                    beginReceiveName(ring);
                }
            }
        }

        /**
         * Receives the name from the client
         * @param ring
         */
        void beginReceiveName(struct io_uring *ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event_ReceiveName;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving the name
         * @param ring
         * @param res
         */
        void onReceiveNameComplete(struct io_uring *ring, const int res) {
            if (res == 0) {
                // no data received
            } else if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {
                clientName.append(readBuffer, res);
                if (clientName.ends_with("\r")) {
                    clientName.erase(clientName.size() - 1, 1);
                    appendId("_");
                    appendId(clientName);
                    printHello();
                    beginSendAck(ring);
                } else {
                    beginReceiveName(ring);
                }
            }
        }

        /**
         * Sends an acknowledgement to the client
         * @param ring
         */
        void beginSendAck(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_send(sqe, fd, "\r", 1, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event_SendAck;
            io_uring_submit(ring);
        }

        void onSendAckComplete(struct io_uring *ring, int res) {
            afterSendAckComplete(ring);
        }
        virtual void afterSendAckComplete(struct io_uring *ring) = 0;

    };
}

#endif //PUBSUBHANDLER_HPP
