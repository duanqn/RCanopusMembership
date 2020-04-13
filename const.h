#ifndef CONST_H_
#define CONST_H_
#include <cstdint>
#include <chrono>

const static int LISTEN_BACKLOG = 5;
const static uint16_t VERSION_1_0 = 1;
const static uint16_t VERSION_LATEST = VERSION_1_0;

const static size_t SBFT_HASH_SIZE = 32;
const static size_t SBFT_SIGNATURE_SIZE = 32;

const static size_t INIT_QUEUE_CAPACITY = 128;
const static size_t REQUEST_BATCH_SIZE = 1024 * 1024;

const static int REQ_PER_SL_PER_SECOND = 10000;

namespace CALC{
    const static size_t REQUEST_SIZE = 256;
    const static float REQUEST_PER_BATCH = REQUEST_BATCH_SIZE / (float) REQUEST_SIZE;
    const static std::chrono::milliseconds REQUEST_BATCH_INTERVAL = std::chrono::milliseconds((int)(1000 * REQUEST_PER_BATCH / REQ_PER_SL_PER_SECOND));
}

const static std::chrono::milliseconds REQUEST_BATCH_INTERVAL = CALC::REQUEST_BATCH_INTERVAL;

#endif