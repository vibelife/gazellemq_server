#include <latch>
#include "server/GazelleServers.hpp"

int main() {
    using namespace gazellemq::server;
    SubscriberServer subscriberServer{5875};
    subscriberServer.start();

    PublisherServer publisherServer{5876};
    publisherServer.start();

    CommandServer commandServer{5877, &subscriberServer};
    commandServer.start();

    std::latch{1}.wait();
    return 0;
}
