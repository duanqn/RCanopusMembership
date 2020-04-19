#include "exception.h"

void DebugFailFast(){
#ifdef DEBUG_FAILFAST
    throw Exception(Exception::EXCEPTION_DEBUG_FAILFAST);
#endif
}
