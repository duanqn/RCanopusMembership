#ifndef CONST_H_
#define CONST_H_
#include <cstdint>
#include <chrono>

const static int LISTEN_BACKLOG = 5;
const static uint16_t VERSION_1_0 = 1;
const static uint16_t VERSION_LATEST = VERSION_1_0;

const static size_t SBFT_HASH_SIZE = 32;
const static size_t SBFT_SIGNATURE_SIZE = 32;
const static size_t SBFT_COMBINED_SIGNATURE_SIZE = 32;

const static size_t INIT_QUEUE_CAPACITY = 128;
const static size_t REQUEST_BATCH_SIZE = 1024 * 1024;

const static size_t REQUEST_SIZE = 256;

const static int MAX_ROUND3_CYCLES = 10;

const static void *POINTER_TO_SELF = nullptr;

#endif
