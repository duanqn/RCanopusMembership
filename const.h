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

const static uint16_t REQUEST_TYPE_FROM_CLIENT = 0;
const static uint16_t REQUEST_TYPE_FETCHED_RESULT = 1;
const static uint16_t REQUEST_TYPE_LOCAL_CONNECTIVITY = 2;
const static uint16_t REQUEST_TYPE_REMOTE_CONNECTIVITY = 3;
const static uint16_t REQUEST_TYPE_LOCAL_MEMBERSHIP = 4;
const static uint16_t REQUEST_TYPE_FULL_MEMBERSHIP = 5;

const static uint16_t INVALID_SENDER_ID = 0xFFFF;

const static std::chrono::milliseconds TIMEOUT_EMULATOR(5000);
const static std::chrono::milliseconds TIMEOUT_ENVOY(5000);

const int PIPE_READ = 0;
const int PIPE_WRITE = 1;

const size_t PIPE_READ_BUFFER = 64;

#endif
