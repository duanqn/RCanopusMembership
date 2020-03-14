#include <cstdio>
#include <chrono>
#include <type_traits>
#include "util.h"

int func(int index){
    printf("In func %d!\n", index);
    return index;
}

void func2(int index){
    printf("In func %d!\n", index);
}

int main(){
    int i = 0;

    AlgoLib::Util::TCleanup t([&](){
        printf("Cleaning up!\n");
        printf("i = %d\n", i);
    });

    AlgoLib::Util::DelayedAsyncExecution job(std::chrono::milliseconds(2000), func, 5);
    AlgoLib::Util::AsyncExecution job2(func2, 1);

    for(; i < 10; ++i){
        for(int j = 0; j < 10000; ++j){
            for(int k = 0; k < 10000; ++k){}
        }
        printf("Hello, world!\n");
    }

    int result = job.waitForResult();

    printf("Joined\n");
    job2.waitForResult();

    printf("Async result = %d\n", result);
    return 0;
}
