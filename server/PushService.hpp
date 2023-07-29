#ifndef GAZELLEMQ_SERVER_PUSHSERVICE_HPP
#define GAZELLEMQ_SERVER_PUSHSERVICE_HPP

#include <condition_variable>
#include <thread>
#include "../lib/MPMCQueue/MPMCQueue.hpp"
#include "DataPush.hpp"
#include "MessageSubscriber.hpp"
#include "Consts.hpp"


namespace gazellemq::server {
    class PushService {
    private:
        rigtorp::MPMCQueue<DataPush*> queue;
        std::vector<MessageSubscriber*> subscribers;

        std::mutex mQueue;
        std::condition_variable cvQueue;
        std::atomic_flag hasPendingData{false};
        std::atomic_flag isRunning{true};
        std::jthread bgThread;
        size_t nbActivePushes{};

    public:
        explicit PushService(size_t queueDepth)
            :queue(queueDepth)
        {}

    public:
        /**
         * Pushes a writeBuffer on to the queue. Subscribers of the writeBuffer will receive it asynchronously.
         * @param messageType
         * @param messageContent
         */
        void push(std::string&& messageType, std::string&& messageContent) {
            std::string msg;
            msg.reserve(messageType.size() + messageContent.size() + 16);

            msg.append(messageType);
            msg.push_back('|');
            msg.append(std::to_string(messageContent.size()));
            msg.push_back('|');
            msg.append(messageContent);

            bool hasSubscribers{false};
            for (auto* subscriber : subscribers) {
                if (subscriber->isSubscribed(messageType)) {
                    hasSubscribers = true;
                    auto* dataPush = new DataPush{};
                    dataPush->writeBuffer.reserve(msg.size());
                    dataPush->writeBuffer.append(msg);
                    dataPush->fd = subscriber->fd;
                    queue.push(dataPush);
                }
            }



            if (hasSubscribers) {
                notify();
            }
        }

        /**
         * Registers a subscriber so it can receive messages
         * @param subscriber
         */
        void registerSubscriber(MessageSubscriber* subscriber) {
            subscribers.emplace_back(subscriber);
        }

        /**
         * Starts watching the work queue
         */
        void start() {
            using namespace std::chrono_literals;

            bgThread = std::jthread{[this]() {
                constexpr static size_t MAX_EVENTS = 16;
                io_uring ring{};
                io_uring_queue_init(MAX_EVENTS, &ring, 0);

                std::vector<io_uring_cqe*> cqes{};
                cqes.reserve(MAX_EVENTS);
                cqes.insert(cqes.begin(), MAX_EVENTS, nullptr);

                __kernel_timespec ts{.tv_sec = 2, .tv_nsec = 0};

                outer:
                while (isRunning.test()) {
                    std::unique_lock uniqueLock{mQueue};
                    cvQueue.wait(uniqueLock, [this]() { return hasPendingData.test(); });

                    uniqueLock.unlock();

                    while (!queue.empty()) {
                        DataPush* dataPush;
                        if (queue.try_pop(dataPush)) {
                            dataPush->handleEvent(&ring, 0);
                            ++nbActivePushes;
                        }
                    }

                    {
                        std::lock_guard lk(mQueue);
                        hasPendingData.clear();
                    }

                    while (isRunning.test()) {
                        if (nbActivePushes == 0) {
                            goto outer;
                        }

                        int ret = io_uring_wait_cqe_timeout(&ring, cqes.data(), &ts);
                        if (ret == -SIGILL) {
                            continue;
                        }


                        if (ret < 0) {
                            if (ret == TIMEOUT) {
                                continue;
                            } else {
                                printError("io_uring_wait_cqe_timeout(...)", ret);
                                return;
                            }
                        }


                        for (auto &cqe: cqes) {
                            if (cqe != nullptr) {
                                int res = cqe->res;
                                if (res == -EAGAIN) {
                                    io_uring_cqe_seen(&ring, cqe);
                                    continue;
                                }

                                auto* push = static_cast<DataPush*>(io_uring_cqe_get_data(cqe));
                                push->handleEvent(&ring, res);
                                if (push->isDone()) {
                                    --nbActivePushes;
                                }
                            }
                            io_uring_cqe_seen(&ring, cqe);
                        }
                    }
                }

                io_uring_queue_exit(&ring);
            }};
        }

    private:
        /**
         * Error handler
         * @param msg
         */
        static void printError(char const* msg, int err) {
            printf("%s\n%s\n", msg, strerror(-err));
        }

        /**
         * Alerts the thread that there is pending data
         */
        void notify() {
            if (!hasPendingData.test()) {
                std::lock_guard lock{mQueue};
                hasPendingData.test_and_set();
                cvQueue.notify_one();
            }
        }
    };

    inline PushService _mq{4096};

    static PushService& getPushService() {
        return gazellemq::server::_mq;
    }
}

#endif //GAZELLEMQ_SERVER_PUSHSERVICE_HPP
