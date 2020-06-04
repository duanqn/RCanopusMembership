#include <cstdio>
#include "config.h"
#include "exception.h"
#include "util.h"
#include "ConnManager.h"
#include <memory>
#include "QueueElement.h"

bool fakeVerifier(char const *msg, size_t msgsize, char const *sig, size_t sigsize){
    for(int i = 0; i < sigsize; ++i){
        if(sig[i] != '\0'){
            return false;
        }
    }

    return true;
}

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
                    throw;
            }
            
            return 1;
        }
    }

    #ifdef DEBUG_WAIT
    pid_t pid = getpid();
    fprintf(stderr, "BG %d SL %d \t PID %d\n", config.BGid, config.SLid, pid);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    #endif

    std::unique_ptr<ConnManager> upMgr;
    printf("Setting up connections...\n");
    try{
        upMgr = std::make_unique<ConnManager>(config);
    }
    catch(Exception &e){
        printf("Exception code %d\n", e.getReason());
        throw;
    }
    
    printf("Connection set up successfully.\n");

    try{
        upMgr->setVerifier(fakeVerifier);
        upMgr->start();
    }
    catch(FailFast &e){
        printf("FailFast at %s:%d\n", e.m_filename, e.m_line);
        fflush(stdout);
        throw;
    }
    catch(Exception &e){
        printf("Exception code %d\n", e.getReason());
        fflush(stdout);
        throw;
    }
    
    return 0;
}