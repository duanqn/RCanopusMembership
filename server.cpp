#include <cstdio>
#include "config.h"
#include "exception.h"
#include "util.h"
#include "ConnManager.h"
#include <memory>

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
    return 0;
}