#ifndef GAZELLEMQ_ENUMS_HPP
#define GAZELLEMQ_ENUMS_HPP

namespace gazellemq::server {
    enum VResult {
        V_SUCCEEDED,
        V_RETRY,
        V_FAILED,
    };

    class Enums {
    public:
        enum Event {
            Event_NotSet,
            Event_Idle,
            Event_Ready,
            Event_SetupPublisherListeningSocket,
            Event_AcceptPublisherConnection,
            Event_SetNonblockingPublisher,
            Event_ReceiveIntent,
            Event_Disconnected,
            Event_ReceivePublisherData,
            Event_ReceiveSubscriptions,
            Event_ReceiveName,
            Event_SendData,
            Event_SendAck,

            Event_ReceiveHttpUpgrade,
            Event_SendWSHandshake
        };
    };
}

#endif //GAZELLEMQ_ENUMS_HPP
