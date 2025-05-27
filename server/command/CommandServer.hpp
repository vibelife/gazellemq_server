#ifndef COMMANDSERVER_HPP
#define COMMANDSERVER_HPP
#include "../BaseServer.hpp"
#include "CommandHandler.hpp"

namespace gazellemq::server {
    class CommandServer final : public BaseServer {
    private:
        SubscriberServer* subscriberServer;
    public:
        CommandServer(
            int const port,
            SubscriberServer* subscriberServer,
            ServerContext* serverContext,
            std::atomic_flag& isRunning,
            std::function<PubSubHandler* (int, ServerContext*)>&& createFn
        ) : BaseServer(port, serverContext, isRunning, std::move(createFn)),
            subscriberServer(subscriberServer)
        {}
    protected:
        void printHello() override {
            std::cout << "Command server started [port " << port << "]" <<std::endl;
        }

        void afterConnectionAccepted(struct io_uring *ring, PubSubHandler* pubSubHandler) override {
            auto connection = dynamic_cast<CommandHandler*>(pubSubHandler);
            connection->setSubscriberServer(subscriberServer);
            connection->handleEvent(ring, epfd);
        }

        static void eventLoop(io_uring* ring, std::vector<io_uring_cqe*>& cqes, __kernel_timespec& ts) {
            const int ret = io_uring_wait_cqe_timeout(ring, cqes.data(), &ts);
            if (ret == -SIGILL || ret == TIMEOUT) {
                return;
            }

            if (ret < 0) {
                printError("io_uring_wait_cqe_timeout(...)", ret);
                exit(0);
            }

            std::ranges::for_each(cqes, [&ring](io_uring_cqe* cqe) {
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

            while (isRunning.test()) {
                eventLoop(ring, cqes, ts);

                removeDisconnectedClients();
            }
        }

        void handleEvent(struct io_uring *ring, const int res) override {
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


#endif //COMMANDSERVER_HPP
