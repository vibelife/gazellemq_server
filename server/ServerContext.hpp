#ifndef SERVERCONTEXT_HPP
#define SERVERCONTEXT_HPP

namespace gazellemq::server {
    class ServerContext {
    private:
        std::vector<std::pair<std::string, std::string>> pendingSubscriptions;
    public:
        void addPendingSubscriptions(std::string &&name, std::string &&subscriptions) {
            pendingSubscriptions.emplace_back(std::move(name), std::move(subscriptions));
        }

        [[nodiscard]] std::vector<std::pair<std::string, std::string>> getPendingSubscriptions() const {
            return pendingSubscriptions;
        }

        std::vector<std::string> takePendingSubscriptions(std::string const& clientName) {
            std::vector<std::string> dest;
            for (const auto&[fst, snd] : pendingSubscriptions) {
                if (fst == clientName) {
                    dest.emplace_back(snd);
                }
            }

            // remove the ones that are about to be sent back to the caller
            std::erase_if(pendingSubscriptions, [&](auto const& p) { return p.first == clientName; });

            return dest;
        }
    };
}

#endif //SERVERCONTEXT_HPP
