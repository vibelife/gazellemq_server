#include <latch>

#include "server/command/CommandServer.hpp"
#include "server/subscriber/SubscriberServer.hpp"
#include "server/publisher/PublisherServer.hpp"

using namespace gazellemq::server;

std::atomic_flag isRunning{true};
std::latch latch{1};

static void signalHandler(const int signal) {
    switch(signal) {
        case SIGINT:
        case SIGTERM: {
            std::cout << "Stopping on next cycle\n" << std::endl << std::flush;
            isRunning.clear();
            latch.count_down();
            break;
        }
        default:
            std::cout << "Not supported. Please use CTRL-C to shutdown the server." << std::endl << std::flush;
        break;
    }
}

static void handleSignal(const int sig) {
    struct sigaction sigIntHandler{};
    sigIntHandler.sa_handler = signalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(sig, &sigIntHandler, nullptr);
}

int main() {
    handleSignal(SIGINT);
    handleSignal(SIGTERM);

    ServerContext serverContext;

    SubscriberServer subscriberServer{5875, &serverContext, isRunning, [](int res, ServerContext* context) {
        return new TCPSubscriberHandler{res, context};
    }};
    subscriberServer.start();

    PublisherServer publisherServer{5876, &serverContext, isRunning, [](int res, ServerContext* context) {
        return new TCPPublisherHandler{res, context};
    }};
    publisherServer.start();

    CommandServer commandServer{5877, &subscriberServer, &serverContext, isRunning, [](int res, ServerContext* context) {
        return new CommandHandler{res, context};
    }};
    commandServer.start();

    latch.wait();
    return 0;
}
