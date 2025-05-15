#ifndef COMMANDHANDLER_HPP
#define COMMANDHANDLER_HPP
#include "PubSubHandler.hpp"
#include "SubscriberServer.hpp"

namespace gazellemq::server {
    class CommandHandler final : public PubSubHandler {
    private:
        bool isNew{true};
        bool isDisconnected{false};
        std::string command;
        SubscriberServer* subscriberServer{nullptr};
    public:
        CommandHandler(int const fd, ServerContext* serverContext) :
            PubSubHandler(fd, serverContext)
        {}

        ~CommandHandler() override = default;

        void printHello() override {
            std::cout << clientName << " | a commander has connected" << std::endl << std::flush;
        }
    protected:
        void afterSendAckComplete(struct io_uring *ring) override {
            beginReceiveData(ring);
        }

        void onDisconnected (int res) override {
            std::cout << "Publisher disconnected [" << clientName << "]\n";
            markForRemoval();
        }
    public:
        void setSubscriberServer(SubscriberServer* subscriberServer) {
            this->subscriberServer = subscriberServer;
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
                command.append(readBuffer, res);

                // check if the command is complete
                if (command.ends_with("\r")) {
                    command.erase(command.size() - 1, 1);

                    // process command
                    processCommand(command);
                    beginSendAck(ring);
                } else {
                    beginReceiveData(ring);
                }
            }
        }

        void processCommand(std::string& commands) const {
            std::vector<std::string> commandValues;
            utils::split(std::string{commands}, commandValues, '\r');

            for (std::string const& command : commandValues) {
                std::vector<std::string> values;
                utils::split(std::string{command}, values, '|');

                if (values.size() == 3) {
                    std::string name{values.at(0)};
                    std::string type{values.at(1)};
                    std::string value{values.at(2)};

                    if (type == "subscribe") {
                        addSubscription(std::move(name), std::move(value));
                    }
                } else {
                    std::cerr << "Invalid command (" << command << ")" << std::endl;
                }
            }

            commands.clear();
        }

        void addSubscription(std::string &&name, std::string &&subscriptions) const {
            bool wasFound{false};
            for (SubscriberHandler *client : subscriberServer->getClients()) {
                if ((client->getClientName() == name) && (!client->getIsDisconnected())) {
                    client->addSubscriptions(subscriptions);
                    wasFound = true;
                }
            }

            if (!wasFound) {
                // to get here means the subscriber probably just hasn't connected yet
                serverContext->addPendingSubscriptions(std::move(name), std::move(subscriptions));
            }
        }
    public:
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
    };
}

#endif //COMMANDHANDLER_HPP
