#ifndef SERVERCONTEXT_HPP
#define SERVERCONTEXT_HPP

namespace gazellemq::server {
    struct PendingSubscription {
        std::string name;
        std::string subscription;
        unsigned long timeoutMs{};

    private:
        void move(PendingSubscription&& other) {
            name = std::move(other.name);
            subscription = std::move(other.subscription);
            timeoutMs = other.timeoutMs;
        }

    public:
        PendingSubscription(std::string name, std::string subscription, unsigned long timeoutMs)
            : name(std::move(name)), subscription(std::move(subscription)), timeoutMs(timeoutMs) {}

        PendingSubscription(PendingSubscription&& other) noexcept {
            move(std::move(other));
        }

        PendingSubscription& operator=(PendingSubscription&& other) noexcept {
            move(std::move(other));
            return *this;
        }

        PendingSubscription& operator=(PendingSubscription const& other) = delete;
        PendingSubscription(PendingSubscription const& other) = delete;
    };

    class ServerContext {
    private:
        std::vector<PendingSubscription> pendingSubscriptions;
    public:
        void addPendingSubscriptions(unsigned long timeoutMs, std::string &&name, std::string &&subscriptions) {
            pendingSubscriptions.emplace_back(std::move(name), std::move(subscriptions), timeoutMs);
        }

        std::vector<PendingSubscription> takePendingSubscriptions(std::string const& clientName) {
            std::vector<PendingSubscription> dest;
            for (PendingSubscription& pendingSubscription : pendingSubscriptions) {
                if (pendingSubscription.name == clientName) {
                    dest.emplace_back(std::move(pendingSubscription));
                }
            }

            // remove the ones that are about to be sent back to the caller
            std::erase_if(pendingSubscriptions, [&](PendingSubscription const& p) { return p.name == clientName; });

            return dest;
        }
    };
}

#endif //SERVERCONTEXT_HPP
