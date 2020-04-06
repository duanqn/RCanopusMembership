#include <cstdio>
#include "config.h"
#include "exception.h"

int main(){
    FILE *fConfig = fopen("test.conf", "r");
    Config config;
    try{
        config = parseFromFile(fConfig);
    }
    catch(Exception &e){
        switch(e.getReason()){
            case Exception::EXCEPTION_INPUT_FORMAT:
                printf("Wrong input format!");
                break;
            default:
                throw e;
        }
        
        return 1;
    }
    
    fclose(fConfig);
    return 0;
}