#include <iostream>
#include "server/Hub.hpp"

int main() {
    gazellemq::server::Hub hub;
    hub.start(5875, 16);
    return 0;
}
