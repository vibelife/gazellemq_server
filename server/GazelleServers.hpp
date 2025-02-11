#ifndef GAZELLEMQ_SERVER_GAZELLESERVERS_HPP
#define GAZELLEMQ_SERVER_GAZELLESERVERS_HPP

#include <liburing.h>
#include <vector>
#include <netinet/in.h>
#include <cstring>
#include <sys/epoll.h>
#include <condition_variable>

#include "Enums.hpp"

namespace gazellemq::server {
    static std::atomic<int> g_id{0};

    class BaseObject {
    protected:
        std::string id{};
    public:
        BaseObject() {
            id.append(std::to_string(g_id++));
        }
    public:
        void appendId(std::string const& val) {
            id.append(val);
        }

        void printId() const {
            printf("%s\n", id.c_str());
        }

        virtual void handleEvent (io_uring* ring, int res) = 0;
    };


    class PubSubHandler : public BaseObject {
    protected:
        static constexpr auto NB_INTENT_CHARS = 2;

        int fd;
        char readBuffer[8192]{};
        std::string intent{};
        Enums::Event event{Enums::Event::Event_NotSet};
        std::string clientName{};
    public:
        explicit PubSubHandler(int res)
                :fd(res)
        {}

        virtual ~PubSubHandler() = default;

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
                    printHello();
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
         * @param client
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
         * Wait for the client to pushToSubscribers indicate whether it is a publisher or subscriber
         * @param client
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
        void onReceiveNameComplete(struct io_uring *ring, int res) {
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


    class PublisherHandler : public PubSubHandler {
    private:
        std::string writeBuffer{};
        std::string nextBatch{};
        std::condition_variable cvQueue;
        rigtorp::MPMCQueue<std::string> queue;

        unsigned int messageBatchSize;

        enum ParseState {
            ParseState_messageType,
            ParseState_messageContentLength,
            ParseState_messageContent,
        };

        ParseState parseState{};

        std::string messageType;
        std::string messageLengthBuffer;
        std::string messageContent;
        size_t messageContentLength{};
        size_t nbContentBytesRead{};

        MessageBatch currentBatch{};
        bool isNew{true};
        bool isDisconnected{false};
    public:
        explicit PublisherHandler(int res)
                : queue(32),
                  messageBatchSize(1),
                  PubSubHandler(res)
        {}

        PublisherHandler(PublisherHandler&& other) noexcept: queue(32), messageBatchSize(1), PubSubHandler(other.fd) {
            swap(std::move(other));
        }

        PublisherHandler& operator=(PublisherHandler&& other) noexcept {
            swap(std::move(other));
            return *this;
        }

        void printHello() override {
            std::cout << "A Publisher has connected\n";
        }

    private:
        void swap(PublisherHandler&& other) {
            std::swap(this->writeBuffer, other.writeBuffer);
            std::swap(this->nextBatch, other.nextBatch);
            // std::swap(this->mQueue, other.mQueue);
            // std::swap(this->cvQueue, other.cvQueue);
            // std::swap(this->hasPendingData, other.hasPendingData);
            // std::swap(this->isRunning, other.isRunning);
            // std::swap(this->queue, other.queue);
            std::swap(this->messageBatchSize, other.messageBatchSize);
            std::swap(this->parseState, other.parseState);
            std::swap(this->fd, other.fd);
            std::swap(this->id, other.id);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->intent, other.intent);
            std::swap(this->event, other.event);
            std::swap(this->clientName, other.clientName);
            std::swap(this->isNew, other.isNew);
            std::swap(this->isDisconnected, other.isDisconnected);
            std::swap(this->messageType, other.messageType);
            std::swap(this->messageLengthBuffer, other.messageLengthBuffer);
            std::swap(this->messageContent, other.messageContent);
            std::swap(this->messageContentLength, other.messageContentLength);
            std::swap(this->nbContentBytesRead, other.nbContentBytesRead);
        }
    protected:
        void handle(struct io_uring *ring, int res) override {
            if (getIsDisconnected()) return;

            switch (event) {
                case Enums::Event_ReceivePublisherData:
                    onReceiveDataComplete(ring, res);
                    break;
                default:
                    break;
            }
        }

        void afterSendAckComplete(struct io_uring *ring) override {
            beginReceiveData(ring);
        }

        void onDisconnected (int res) override {
            std::cout << "Publisher disconnected [" << clientName << "]\n";
            markForRemoval();
        }
    public:
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

    private:
        void pushToQueue(MessageBatch&& batch) {
            getMessageQueue().push_back(std::move(batch));
        }

        void beginReceiveData(struct io_uring* ring) {
            memset(readBuffer, 0, MAX_READ_BUF);
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event_ReceivePublisherData;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving data
         * @param ring
         * @param res
         */
        void onReceiveDataComplete(struct io_uring *ring, int res) {
            if (res <= 0) {
                // The client has disconnected
                beginDisconnect(ring);
            } else {
                forwardMessage(readBuffer, res);

                beginReceiveData(ring);
            }
        }

        /**
         * forwards the message to subscribers
         * @param buffer
         * @param bufferLength
         */
        void forwardMessage(char const* buffer, size_t bufferLength) {
            for (size_t i{0}; i < bufferLength; ++i) {
                char ch {buffer[i]};
                if (parseState == ParseState_messageType) {
                    if (ch == '|') {
                        parseState = ParseState_messageContentLength;
                        continue;
                    } else {
                        messageType.push_back(ch);
                    }
                } else if (parseState == ParseState_messageContentLength) {
                    if (ch == '|') {
                        messageContentLength = std::stoul(messageLengthBuffer);
                        parseState = ParseState_messageContent;
                        continue;
                    } else {
                        messageLengthBuffer.push_back(ch);
                    }
                } else if (parseState == ParseState_messageContent) {
                    size_t nbCharsNeeded {messageContentLength - nbContentBytesRead};
                    // add as many characters as possible in bulk

                    if ((i + nbCharsNeeded) <= bufferLength) {
                        messageContent.append(&buffer[i], nbCharsNeeded);
                        // do nothing here for now
                    } else {
                        nbCharsNeeded = bufferLength - i;
                        messageContent.append(&buffer[i], nbCharsNeeded);
                    }

                    nbContentBytesRead += nbCharsNeeded;
                    i += nbCharsNeeded - 1;

                    if (messageContentLength == nbContentBytesRead) {
                        // Done parsing
                        std::string message{};
                        message.append(messageType);
                        message.push_back('|');
                        message.append(messageLengthBuffer);
                        message.push_back('|');
                        message.append(messageContent);

                        if (currentBatch.getMessageType().empty()) {
                            currentBatch.setMessageType(messageType);
                        } else if ((currentBatch.getMessageType() != messageType) || (currentBatch.isFull())) {
                            pushToQueue(std::move(currentBatch));
                            currentBatch.clearForNextMessage();
                            currentBatch.setMessageType(messageType);
                        }

                        currentBatch.append(std::move(message));

                        if (i == (bufferLength-1)) {
                            pushToQueue(std::move(currentBatch));
                            currentBatch.clearForNextMessage();
                        }


                        // fnPushToQueue(messageType, std::move(message));
                        messageContentLength = 0;
                        nbContentBytesRead = 0;
                        messageLengthBuffer.clear();
                        messageType.clear();
                        messageContent.clear();
                        parseState = ParseState_messageType;
                    }
                }
            }
        }
    };


    class SubscriberHandler : public PubSubHandler {
    private:
        std::vector<std::string> subscriptions;
        std::string subscriptionsBuffer;
        std::list<MessageBatch> pendingItems;
        MessageBatch currentItem{};
        bool isNew{true};
        bool isDisconnected{false};
    public:
        explicit SubscriberHandler(int res)
            : PubSubHandler(res)
        {}

        SubscriberHandler(SubscriberHandler const&) = delete;
        SubscriberHandler& operator=(SubscriberHandler const&) = delete;

        SubscriberHandler(SubscriberHandler&& other) noexcept: PubSubHandler(other.fd) {
            swap(std::move(other));
        }

        SubscriberHandler& operator=(SubscriberHandler&& other) noexcept {
            swap(std::move(other));
            return *this;
        }

        void printHello() override {
            std::cout << "A Subscriber has connected\n";
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
    private:
        void swap(SubscriberHandler&& other) {
            std::swap(this->subscriptions, other.subscriptions);
            std::swap(this->subscriptionsBuffer, other.subscriptionsBuffer);
            std::swap(this->pendingItems, other.pendingItems);
            std::swap(this->currentItem, other.currentItem);
            std::swap(this->fd, other.fd);
            std::swap(this->id, other.id);
            std::swap(this->readBuffer, other.readBuffer);
            std::swap(this->intent, other.intent);
            std::swap(this->event, other.event);
            std::swap(this->clientName, other.clientName);
            std::swap(this->isNew, other.isNew);
            std::swap(this->isDisconnected, other.isDisconnected);
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
            beginReceiveSubscriptions(ring);
        }

        void handle(struct io_uring *ring, int res) override {
            if (getIsDisconnected()) return;

            switch (event) {
                case Enums::Event_ReceiveSubscriptions:
                    onReceiveSubscriptionsComplete(ring, res);
                    break;
                case Enums::Event_SendData:
                    onSendCurrentMessageComplete(ring, res);
                    break;
                default:
                    break;
            }
        }

        /**
         * Receives subscriptions from the subscriber
         * @param ring
         */
        void beginReceiveSubscriptions(struct io_uring *ring) {
            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recv(sqe, fd, readBuffer, MAX_READ_BUF, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event_ReceiveSubscriptions;
            io_uring_submit(ring);
        }

        /**
         * Checks if we are done receiving data
         * @param ring
         * @param res
         */
        void onReceiveSubscriptionsComplete(struct io_uring *ring, int res) {
            if (res < 0) {
                beginDisconnect(ring);
            } else {
                subscriptionsBuffer.append(readBuffer, strlen(readBuffer));
                if (subscriptionsBuffer.ends_with('\r')) {
                    subscriptionsBuffer.erase(subscriptionsBuffer.size() - 1, 1);
                    gazellemq::utils::split(std::move(subscriptionsBuffer), subscriptions);
                    event = Enums::Event_Ready;
                } else {
                    beginReceiveSubscriptions(ring);
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


    template <typename T>
    class BaseServer : public BaseObject {
    protected:
        static constexpr auto TIMEOUT = -62;

        int fd{};
        int port;
        int epfd{};
        struct sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        std::vector<T*> clients{};
        Enums::Event event{Enums::Event::Event_NotSet};
        unsigned int maxEventBatch{8};
        std::jthread bgThread;
    public:
        explicit BaseServer(int port)
            :port(port)
        {}

        virtual ~BaseServer() {
            while (!clients.empty()) {
                delete clients.at(0);
                clients.erase(clients.begin());
            }
        }

        BaseServer(BaseServer const&) = delete;
        BaseServer& operator=(BaseServer const&) = delete;

        BaseServer(BaseServer&& other) noexcept: port(other.port) {
            swap(std::move(other));
        }

        BaseServer& operator=(BaseServer&& other) noexcept {
            swap(std::move(other));
            return *this;
        }
    protected:
        void swap(BaseServer&& other) {
            std::swap(this->fd, other.fd);
            std::swap(this->port, other.port);
            std::swap(this->epfd, other.epfd);
            std::swap(this->clientAddr, other.clientAddr);
            std::swap(this->clientAddrLen, other.clientAddrLen);
            std::swap(this->event, other.event);
            std::swap(this->maxEventBatch, other.maxEventBatch);
        }

        void removeDisconnectedClients() {
            //clients.erase(
            //        std::remove_if(clients.begin(), clients.end(), [](T& o) {
            //            return !o.getIsNew() && o.getIsDisconnected();
            //        }), clients.end());
        }

        [[nodiscard]] bool anyNew() const {
            return std::any_of(clients.begin(), clients.end(), [](T const& o) {
                return o.getIsNew();
            });
        }

        [[nodiscard]] bool allIdle() const {
            return std::all_of(clients.begin(), clients.end(), [](T const* o) {
                return o->getIsIdle();
            });
        }


        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Sets up the listening socket
         * @param ring
         * @return
         */
        void beginSetupListenerSocket(struct io_uring *ring) {
            fd = socket(PF_INET, SOCK_STREAM, 0);
            if (fd == -1) {
                printf("%s\n", "socket(...)");
                exit(1);
            }

            struct sockaddr_in srv_addr{};
            memset(&srv_addr, 0, sizeof(srv_addr));

            srv_addr.sin_family = AF_INET;
            srv_addr.sin_port = htons(port);
            srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            int optVal = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

            // We bind to a port and turn this socket into a listening socket.
            int ret = bind(fd, (const struct sockaddr *) &srv_addr, sizeof(srv_addr));
            if (ret < 0) {
                printf("%s\n", "bind(...)");
                exit(1);
            }

            ret = listen(fd, 10);
            if (ret < 0) {
                printf("%s\n", "listen(...)");
                exit(1);
            }

            // set up polling of the inotify
            epfd = epoll_create1(0);
            if (epfd < 0) {
                printf("%s\n", "epoll_create1(...)");
                exit(1);
            }


            io_uring_sqe *sqe = io_uring_get_sqe(ring);
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR | EPOLLRDHUP;
            ev.data.fd = fd;
            io_uring_prep_epoll_ctl(sqe, epfd, fd, EPOLL_CTL_ADD, &ev);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_SetupPublisherListeningSocket;
            io_uring_submit(ring);
        }

        /**
         * Completes the setup event and goes to the accept event
         * @param res
         */
        void onSetupListeningSocketComplete(struct io_uring *ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__, res);
            } else {
                printHello();
                beginAcceptConnection(ring);
            }
        }

        /**
         * Submits an [accept] system call using liburing
         * @param ring
         */
        void beginAcceptConnection(struct io_uring *ring) {
            io_uring_sqe *sqe = io_uring_get_sqe(ring);
            io_uring_prep_accept(sqe, fd, (sockaddr *) &clientAddr, &clientAddrLen, 0);
            io_uring_sqe_set_data(sqe, this);

            event = Enums::Event::Event_AcceptPublisherConnection;
            io_uring_submit(ring);
        }

        /**
         * Accepts an incoming connection, then makes the connection non blocking.
         * @param ring
         * @param res
         */
        void onAcceptConnectionComplete(struct io_uring *ring, int res) {
            if (res < 0) {
                printError(__PRETTY_FUNCTION__, res);
            } else {
                // listen for more connections
                beginAcceptConnection(ring);

                // A client has connected
                auto client = new T{res};
                afterConnectionAccepted(ring, client);
                clients.emplace_back(client);
            }
        }

        virtual void printHello() = 0;

        virtual void afterConnectionAccepted(struct io_uring *ring, T* connection) = 0;

        virtual void doEventLoop(io_uring* ring) = 0;
    public:
        void start() {
            bgThread = std::jthread{[this]() {
                struct io_uring ring{};
                io_uring_queue_init(8, &ring, 0);
                handleEvent(&ring, 0);

                doEventLoop(&ring);
            }};
        }
    };


    class SubscriberServer : public BaseServer<SubscriberHandler> {
    private:
    public:
        SubscriberServer(int const port)
            : BaseServer<SubscriberHandler>(port)
        {}
    protected:
        void printHello() override {
            std::cout << "Subscriber server started [port " << port << "]" <<std::endl;
        }

        void afterConnectionAccepted(struct io_uring *ring, SubscriberHandler* connection) override {
            connection->handleEvent(ring, epfd);
        }

        bool drainQueue(io_uring* ring, MessageQueue& q) {
            MessageBatch batch;
            while (q.try_pop(batch)) {
                    std::for_each(clients.begin(), clients.end(), [&batch, &ring](SubscriberHandler* subscriber) {
                        if (subscriber->isSubscribed(batch.getMessageType())) {
                            subscriber->pushMessageBatch(ring, batch);
                        }
                    });
            }

            q.afQueue.clear();
            {
                std::lock_guard lockGuard{q.mQueue};
                if (q.hasPendingData.test()) {
                    q.hasPendingData.clear();
                    q.cvQueue.notify_all();
                }
            }
        }

        void eventLoop(io_uring* ring, std::vector<io_uring_cqe *>& cqes, __kernel_timespec& ts) {
            int ret = io_uring_wait_cqe_timeout(ring, cqes.data(), &ts);
            if (ret == -SIGILL) {
                return;
            }

            if (ret < 0) {
                if (ret == TIMEOUT) {
                    return;
                } else {
                    printError("io_uring_wait_cqe_timeout(...)", ret);
                    return;
                }
            }

            for (auto* cqe: cqes) {
                if (cqe != nullptr) {
                    int res = cqe->res;
                    if (res == -EAGAIN) {
                        io_uring_cqe_seen(ring, cqe);
                        continue;
                    }

                    auto* pObject = static_cast<BaseObject*>(io_uring_cqe_get_data(cqe));
                    pObject->handleEvent(ring, res);
                }
                io_uring_cqe_seen(ring, cqe);
            }

            removeDisconnectedClients();
        }

        /**
         * Does the event loop
         */
        void doEventLoop(io_uring* ring) override {
            using namespace std::chrono_literals;

            std::vector<io_uring_cqe *> cqes{};
            cqes.reserve(maxEventBatch);
            cqes.insert(cqes.begin(), maxEventBatch, nullptr);

            __kernel_timespec ts{.tv_sec = 1, .tv_nsec = 0};
            auto& q = getMessageQueue();

            while (q.isRunning.test()) {
                while (q.isRunning.test()) {

                    eventLoop(ring, cqes, ts);

                    // if is nothing left to do then break
                    if (allIdle()) {
                        break;
                    }
                }

                while (q.isRunning.test()) {
                    // wait with a timeout for new messages
                    std::unique_lock uniqueLock{q.mQueue};
                    bool didTimeout{!q.cvQueue.wait_for(uniqueLock, 1s, [&]() { return q.hasPendingData.test(); })};

                    uniqueLock.unlock();

                    if (didTimeout || !drainQueue(ring, q)) {
                        break;
                    }

                    eventLoop(ring, cqes, ts);

                    if (allIdle()) {
                        break;
                    }
                }
            }
        }

        void handleEvent(struct io_uring *ring, int res) override {
            switch (event) {
                case Enums::Event::Event_NotSet:
                    beginSetupListenerSocket(ring);
                    break;
                case Enums::Event::Event_SetupPublisherListeningSocket:
                    onSetupListeningSocketComplete(ring, res);
                    break;
                case Enums::Event::Event_AcceptPublisherConnection:
                    onAcceptConnectionComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };


    class PublisherServer : public BaseServer<PublisherHandler> {
    public:
        PublisherServer(int const port)
            : BaseServer<PublisherHandler>(port)
        {}
    protected:
        void printHello() override {
            std::cout << "Publisher server started [port " << port << "]" <<std::endl;
        }

        void afterConnectionAccepted(struct io_uring *ring, PublisherHandler* connection) override {
            connection->handleEvent(ring, epfd);
        }

        void eventLoop(io_uring* ring, std::vector<io_uring_cqe*>& cqes, __kernel_timespec& ts) {
            int ret = io_uring_wait_cqe_timeout(ring, cqes.data(), &ts);
            if (ret == -SIGILL || ret == TIMEOUT) {
                return;
            }

            if (ret < 0) {
                printError("io_uring_wait_cqe_timeout(...)", ret);
                exit(0);
            }

            std::for_each(cqes.begin(), cqes.end(), [&ring](io_uring_cqe* cqe) {
                if (cqe != nullptr) {
                    int res = cqe->res;
                    if (res != -EAGAIN) {
                        auto *pSubscriber = static_cast<BaseObject *>(io_uring_cqe_get_data(cqe));
                        pSubscriber->handleEvent(ring, res);
                    }
                    io_uring_cqe_seen(ring, cqe);
                }
            });
        }

        void doEventLoop(io_uring* ring) override {
            constexpr static size_t NB_EVENTS = 32;

            std::vector<io_uring_cqe*> cqes{};
            cqes.reserve(NB_EVENTS);
            cqes.insert(cqes.begin(), NB_EVENTS, nullptr);

            __kernel_timespec ts{.tv_sec = 1, .tv_nsec = 0};

            auto& q = getMessageQueue();

            while (q.isRunning.test()) {
                eventLoop(ring, cqes, ts);

                removeDisconnectedClients();
            }
        }

        void handleEvent(struct io_uring *ring, int res) override {
            switch (event) {
                case Enums::Event::Event_NotSet:
                    beginSetupListenerSocket(ring);
                    break;
                case Enums::Event::Event_SetupPublisherListeningSocket:
                    onSetupListeningSocketComplete(ring, res);
                    break;
                case Enums::Event::Event_AcceptPublisherConnection:
                    onAcceptConnectionComplete(ring, res);
                    break;
                default:
                    break;
            }
        }
    };
}
#endif //GAZELLEMQ_SERVER_GAZELLESERVERS_HPP
