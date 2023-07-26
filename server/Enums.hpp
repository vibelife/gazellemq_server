#ifndef GAZELLEMQ_ENUMS_HPP
#define GAZELLEMQ_ENUMS_HPP

namespace gazellemq::server {
    class Enums {
    public:
        enum Event {
            Event_NotSet,
            Event_Idle,
            Event_SetupPublisherListeningSocket,
            Event_AcceptPublisherConnection,
            Event_SetNonblockingPublisher,
            Event_ReceiveIntent,
            Event_Disconnected,
            Event_ReceivePublisherData,
        };
    };
}

#endif //GAZELLEMQ_ENUMS_HPP
