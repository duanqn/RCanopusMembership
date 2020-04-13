#include <cstdio>
#include "config.h"
#include "exception.h"
#include "util.h"
#include "ConnManager.h"
#include <memory>
#include "QueueElement.h"

int main(){
    Config config;
    {
        FILE *fConfig = fopen("test.conf", "r");
        AlgoLib::Util::TCleanup t([fConfig]{
            fclose(fConfig);
        });

        try{
            config = parseFromFile(fConfig);
        }
        catch(Exception &e){
            switch(e.getReason()){
                case Exception::EXCEPTION_INPUT_FORMAT:
                    printf("Wrong input format!\n");
                    break;
                default:
                    throw e;
            }
            
            return 1;
        }
    }

    std::unique_ptr<ConnManager> upMgr;
    printf("Setting up connections...\n");
    try{
        upMgr = std::make_unique<ConnManager>(config);
    }
    catch(Exception &e){
        printf("Exception code %X\n", e.getReason());
        throw e;
    }
    
    printf("Connection set up successfully.\n");

    upMgr->start();
    return 0;
}