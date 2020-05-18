#ifndef CYCLE_STATUS_H_
#define CYCLE_STATUS_H_
#include <cstdint>
#include <vector>
#include <memory>
#include "message.h"
#include "QueueElement.h"

enum class CycleState: int{
    NOT_STARTED,
    ROUND2_WAITING_FOR_PREPREPARE,
    ROUND2_COLLECTOR_WAITING_FOR_PARTIALCOMMITS,
    ROUND2_WAITING_FOR_FULLCOMMIT,
    ROUND2_COMMITTED,
    ROUND3_WAITING_FOR_REPLICATED_RESULTS,
    ROUND3_WAITING_FOR_LOCAL_CONNECTIVITY,
    ROUND3_WAITING_FOR_REPLICATED_CONNECTIVITY,
    ROUND3_WAITING_FOR_LOCAL_MEMBERSHIP,
    ROUND3_WAITING_FOR_REPLICATED_MEMBERSHIP,
    ROUND3_COMMITTED,
    ROUND3_WAITING_FOR_PREPREPARE_BASELINE,
    ROUND3_COLLECTOR_WAITING_FOR_PARTIALCOMMITS_BASELINE,
    ROUND3_WAITING_FOR_FULLCOMMIT_BASELINE,
};

struct CycleStatus{
    int message_received_counter;
    int message_required;
    uint16_t awaiting_message_type;
    uint16_t round2_view;
    uint16_t round2_sequence;
    CycleState state;

    std::vector<std::unique_ptr<QueueElement>> rgMsgRound3FetchResponse;
    std::vector<std::unique_ptr<QueueElement>> rgMsgRound3ConnectivityResponse;
    std::vector<std::unique_ptr<QueueElement>> rgMsgRound3MembershipResponse;

    CycleStatus(int numBG):
        state(CycleState::NOT_STARTED),
        message_received_counter(0),
        message_required(0),
        awaiting_message_type(MESSAGE_INVALID),
        rgMsgRound3FetchResponse(numBG),
        rgMsgRound3ConnectivityResponse(numBG),
        rgMsgRound3MembershipResponse(numBG){

    }

    CycleStatus(const CycleStatus &) = delete;
    CycleStatus& operator =(const CycleStatus &) = delete;
    CycleStatus& operator =(CycleStatus &&) = delete;

    CycleStatus(CycleStatus&& status):
        state(std::move(status.state)),
        message_received_counter(std::move(status.message_received_counter)),
        message_required(std::move(status.message_required)),
        awaiting_message_type(std::move(status.awaiting_message_type)),
        rgMsgRound3FetchResponse(status.rgMsgRound3FetchResponse.size()),
        rgMsgRound3ConnectivityResponse(status.rgMsgRound3ConnectivityResponse.size()),
        rgMsgRound3MembershipResponse(status.rgMsgRound3MembershipResponse.size()){

        for(int i = 0; i < rgMsgRound3FetchResponse.size(); ++i){
            rgMsgRound3FetchResponse[i] = std::move(status.rgMsgRound3FetchResponse[i]);
        }

        for(int i = 0; i < rgMsgRound3ConnectivityResponse.size(); ++i){
            rgMsgRound3ConnectivityResponse[i] = std::move(status.rgMsgRound3ConnectivityResponse[i]);
        }

        for(int i = 0; i < rgMsgRound3MembershipResponse.size(); ++i){
            rgMsgRound3MembershipResponse[i] = std::move(status.rgMsgRound3MembershipResponse[i]);
        }
    }
};

#endif