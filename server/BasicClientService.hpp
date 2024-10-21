#ifndef GAZELLEMQ_SERVER_BASICCLIENTSERVICE_HPP
#define GAZELLEMQ_SERVER_BASICCLIENTSERVICE_HPP

#include <vector>
#include <list>
#include <string>

#include "Consts.hpp"
#include "MessageBatch.hpp"

namespace gazellemq::server {
    class BasicClientService {
    private:
        std::vector<std::string> subscriptions;
        char readBuffer[MAX_READ_BUF]{};
        std::string subscriptionsBuffer;
        std::list<MessageBatch> pendingItems;
        MessageBatch currentItem{};
    public:
        
    };
}

#endif //GAZELLEMQ_SERVER_BASICCLIENTSERVICE_HPP
