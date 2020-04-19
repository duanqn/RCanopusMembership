#include "readerwriterqueue.h"
#include "QueueElement.h"
#include "util.h"
#include "const.h"

const static int TEST_NUM = 10000000;

void consumer(moodycamel::BlockingReaderWriterQueue<QueueElement>* pQueue){
    QueueElement res;
    for(int i = 0; i < TEST_NUM; ++i){
        res.pMessage = nullptr;
        pQueue->wait_dequeue(res);
        uint32_t content = *(uint32_t *)((char *)res.pMessage + sizeof(MessageHeader));
        if(content != (uint32_t)i){
            printf("Expecting %u but got %u\n", (uint32_t)i, content);
            throw std::string("Corrupt element!\n");
            return;
        }
    }
}

void producer(moodycamel::BlockingReaderWriterQueue<QueueElement>* pQueue){
    for(int i = 0; i < TEST_NUM; ++i){
        QueueElement element;
        element.pMessage = (MessageHeader *)new char[sizeof(MessageHeader) + 4];
        element.pMessage->version = VERSION_LATEST;
        element.pMessage->msgType = MESSAGE_INVALID;   // For test only
        element.pMessage->payloadLen = 4;
        *(uint32_t *)((char *)element.pMessage + sizeof(MessageHeader)) = (uint32_t)i;

        pQueue->enqueue(std::move(element));
    }
}

int main(){
    moodycamel::BlockingReaderWriterQueue<QueueElement> queue(100);

    QueueElement test;
    test.pMessage = (MessageHeader *)new char[sizeof(MessageHeader) + 4];
    test.pMessage->version = VERSION_LATEST;
    test.pMessage->msgType = MESSAGE_INVALID;   // For test only
    test.pMessage->payloadLen = 4;
    *(uint32_t *)((char *)test.pMessage + sizeof(MessageHeader)) = 0xDEADBEEF;

    queue.enqueue(std::move(test));

    QueueElement res;
    queue.wait_dequeue(res);

    uint32_t content = *(uint32_t *)((char *)res.pMessage + sizeof(MessageHeader));
    if(content != 0xDEADBEEF){
        printf("Single value test failed.\n");
    }

    AlgoLib::Util::DelayedAsyncExecution job(std::chrono::milliseconds(300), producer, &queue);
    try{
        consumer(&queue);
    }
    catch(std::string &e){
        printf("%s\n", e.c_str());
    }

    job.waitForResult();

    printf("Test passed successfully.\n");
    
    return 0;
}