#include <latch>
#include "server/GazelleServers.hpp"

int main() {
    using namespace gazellemq::server;

    ServerContext serverContext;

    SubscriberServer subscriberServer{5875, &serverContext};
    subscriberServer.start();

    PublisherServer publisherServer{5876, &serverContext};
    publisherServer.start();

    CommandServer commandServer{5877, &subscriberServer, &serverContext};
    commandServer.start();

    std::latch{1}.wait();
    return 0;
}
