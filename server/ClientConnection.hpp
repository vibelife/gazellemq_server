#ifndef GAZELLEMQ_CLIENTCONNECTION_HPP
#define GAZELLEMQ_CLIENTCONNECTION_HPP

#include <liburing.h>
#include <sys/epoll.h>
#include "EventLoopObject.hpp"
#include "MessageHandler.hpp"
#include "Consts.hpp"

namespace gazellemq::server {
    class ClientConnection: public EventLoopObject {
    private:
        constexpr static auto PUBLISHER_INTENT = "P\r";
        constexpr static auto SUBSCRIBER_INTENT = "S\r";
        constexpr static auto NB_INTENT_CHARS = 2;

        enum ClientConnectEvent {
            ClientConnectEvent_NotSet,
            ClientConnectEvent_Disconnected,
            ClientConnectEvent_SetNonblockingPublisher,
            ClientConnectEvent_ReceiveIntent,
            ClientConnectEvent_ReceiveName,
        };

        ClientConnectEvent m_event{ClientConnectEvent_NotSet};
        char readBuffer[MAX_READ_BUF]{};
        std::string intent{};
        int fd{};
        MessageHandler* handler = nullptr;


    public:
        virtual ~ClientConnection() {
            delete handler;
        }

        void start(struct io_uring* ring, int epfd, int fileDescriptor) {
            this->fd = fileDescriptor;
            beginMakeNonblockingSocket(ring, epfd);
        }
    private:
        /**
         * Disconnects from the server
         * @param ring
         */
        void beginDisconnect(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_close(sqe, fd);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            m_event = ClientConnectEvent_Disconnected;
            io_uring_submit(ring);
        }

        /**
         * Sets the client connection to non blocking
         * @param ring
         * @param client
         */
        void beginMakeNonblockingSocket(struct io_uring* ring, int epfd) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.fd = fd;
            io_uring_prep_epoll_ctl(sqe, epfd, fd, EPOLL_CTL_ADD, &ev);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            m_event = ClientConnectEvent_SetNonblockingPublisher;
            io_uring_submit(ring);
        }

        /**
         * This client must now indicate whether it is a publisher or subscriber
         * @param ring
         * @param client
         * @param res
         */
        void onMakeNonblockingSocketComplete(struct io_uring* ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {

                // client must communicate with server
                this->beginReceiveIntent(ring);
            }
        }

        /**
         * Wait for the client to push indicate whether it is a publisher or subscriber
         * @param client
         */
        void beginReceiveIntent(struct io_uring* ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, 2, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            m_event = ClientConnectEvent_ReceiveIntent;
            io_uring_submit(ring);
        }

        /**
         * Intent data received from the client
         * @param ring
         * @param res
         */
        void onIntentReceived(struct io_uring *ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__ , res);
                beginDisconnect(ring);
            } else {
                intent.append(readBuffer, res);
                if (intent.size() < NB_INTENT_CHARS) {
                    beginReceiveIntent(ring);
                } else {
                    // check if this is a subscriber or publisher
                    if (intent == PUBLISHER_INTENT) {
                        this->handler = new MessagePublisher(fd);
                    } else if (intent == SUBSCRIBER_INTENT) {
                        this->handler = new MessageSubscriber(fd);
                        getPushService().registerSubscriber(dynamic_cast<MessageSubscriber*>(this->handler));
                    }

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
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, (EventLoopObject*)this);

            m_event = ClientConnectEvent_ReceiveName;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving the name
         * @param ring
         * @param res
         */
        void onReceiveNameComplete(struct io_uring *ring, int res) {
            handler->clientName.append(readBuffer, res);
            if (handler->clientName.ends_with("\r")) {
                handler->clientName.erase(handler->clientName.size() - 1, 1);
                handler->printHello();
                handler->handleEvent(ring, 0);
            } else {
                beginReceiveName(ring);
            }
        }

        void handleEvent(struct io_uring *ring, int res) override {
            switch (m_event) {
                case ClientConnectEvent_SetNonblockingPublisher:
                    onMakeNonblockingSocketComplete(ring, res);
                    break;
                case ClientConnectEvent_ReceiveIntent:
                    onIntentReceived(ring, res);
                    break;
                case ClientConnectEvent_ReceiveName:
                    onReceiveNameComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}

#endif //GAZELLEMQ_CLIENTCONNECTION_HPP
