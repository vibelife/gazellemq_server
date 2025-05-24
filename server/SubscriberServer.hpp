#ifndef SUBSCRIBERSERVER_HPP
#define SUBSCRIBERSERVER_HPP
#include "BaseServer.hpp"
#include "MessageQueue.hpp"
#include "SubscriberHandler.hpp"

namespace gazellemq::server {
    class SubscriberServer final : public BaseServer {
    private:
    public:
        SubscriberServer(
                int const port,
                ServerContext* serverContext,
                std::atomic_flag& isRunning,
                std::function<PubSubHandler* (int, ServerContext*)>&& createFn
                )
            : BaseServer(port, serverContext, isRunning, std::move(createFn))
        {}
    protected:
        void printHello() override {
            std::cout << "Subscriber server started [port " << port << "]" <<std::endl;
        }

        void afterConnectionAccepted(struct io_uring *ring, PubSubHandler* pubSubHandler) override {
            auto connection = dynamic_cast<SubscriberHandler*>(pubSubHandler);
            connection->setServerContext(serverContext);
            connection->handleEvent(ring, epfd);
        }

        bool drainQueue(io_uring* ring, MessageQueue& q) {
            MessageBatch batch;
            bool retVal {false};
            while (q.try_pop(batch)) {
                std::ranges::for_each(clients, [&](PubSubHandler* pubSubHandler) {
                    auto subscriber = dynamic_cast<SubscriberHandler*>(pubSubHandler);
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

            return retVal;
        }

        static void eventLoop(io_uring* ring, std::vector<io_uring_cqe *>& cqes, __kernel_timespec& ts) {
            int ret = io_uring_wait_cqe_timeout(ring, cqes.data(), &ts);
            if (ret == -SIGILL || ret == TIMEOUT) {
                return;
            }

            if (ret < 0) {
                printError("io_uring_wait_cqe_timeout(...)", ret);
                return;
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

            while (isRunning.test()) {
                while (isRunning.test()) {
                    // for the most part, this loop will handle new connections
                    eventLoop(ring, cqes, ts);

                    // if is nothing left to do then break
                    if (allIdle()) {
                        break;
                    }
                }

                while (isRunning.test()) {
                    // for most part, this loop will handle the sending of messages
                    std::unique_lock uniqueLock{q.mQueue};
                    const bool didTimeout{!q.cvQueue.wait_for(uniqueLock, 1s, [&]() { return q.hasPendingData.test(); })};

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

#endif //SUBSCRIBERSERVER_HPP
