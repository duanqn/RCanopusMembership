#include <cstdio>
#include "util.h"

int main(){
    int i = 5;

    AlgoLib::Util::TCleanup t([&](){
        printf("%d\n", i);
    });

    printf("Hello, world!\n");
    return 0;
}