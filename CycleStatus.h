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
    ROUND2_WAITING_FOR_FULLCOMMITS,
    ROUND2_COMMITTED,
};

struct CycleStatus{
    int message_received_counter;
    int message_required;
    uint16_t awaiting_message_type;
    uint16_t round2_view;
    uint16_t round2_sequence;
    CycleState state;

    std::vector<std::unique_ptr<QueueElement>> committedResult;

    CycleStatus(int numBG):
        state(CycleState::NOT_STARTED),
        message_received_counter(0),
        message_required(0),
        awaiting_message_type(MESSAGE_INVALID),
        committedResult(numBG){

    }

    CycleStatus(const CycleStatus &) = delete;
    CycleStatus& operator =(const CycleStatus &) = delete;
    CycleStatus& operator =(CycleStatus &&) = delete;

    CycleStatus(CycleStatus&& status):
        state(std::move(status.state)),
        message_received_counter(std::move(status.message_received_counter)),
        message_required(std::move(status.message_required)),
        awaiting_message_type(std::move(status.awaiting_message_type)),
        committedResult(status.committedResult.size()){

        for(int i = 0; i < committedResult.size(); ++i){
            committedResult[i] = std::move(status.committedResult[i]);
        }
    }
};

#endif