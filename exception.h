#ifndef EXCEPTION_H_
#define EXCEPTION_H_

class Exception{
    protected:
    int reason_code;
    public:
    explicit Exception(int code): reason_code(code){}
    virtual ~Exception(){}

    int getReason() const {
        return reason_code;
    }

    const static int EXCEPTION_BASE = 0x1C00;
    const static int EXCEPTION_DEBUG_FAILFAST = EXCEPTION_BASE + 1;
    const static int EXCEPTION_FASTBREAK = EXCEPTION_DEBUG_FAILFAST + 1;
    const static int EXCEPTION_FASTCONTINUE = EXCEPTION_FASTBREAK + 1;
    const static int EXCEPTION_INPUT_FORMAT = EXCEPTION_FASTCONTINUE + 1;
    const static int EXCEPTION_SOCKET_CREATION = EXCEPTION_INPUT_FORMAT + 1;
    const static int EXCEPTION_SOCKET_OPT = EXCEPTION_SOCKET_CREATION + 1;
    const static int EXCEPTION_SOCKET_BINDING = EXCEPTION_SOCKET_OPT + 1;
    const static int EXCEPTION_MESSAGE_CREATION_TOOLONG = EXCEPTION_SOCKET_BINDING + 1;
    const static int EXCEPTION_MESSAGE_SERIALIZATION_BUFFER_OVERFLOW = EXCEPTION_MESSAGE_CREATION_TOOLONG + 1;
    const static int EXCEPTION_ILLEGAL_TEMPLATE_PARAMETER = EXCEPTION_MESSAGE_SERIALIZATION_BUFFER_OVERFLOW + 1;
    const static int EXCEPTION_SEND = EXCEPTION_ILLEGAL_TEMPLATE_PARAMETER + 1;
    const static int EXCEPTION_RECV = EXCEPTION_SEND + 1;
    const static int EXCEPTION_MESSAGE_INVALID_VERSION = EXCEPTION_RECV + 1;
    const static int EXCEPTION_MESSAGE_INCOMPLETE_HEADER = EXCEPTION_MESSAGE_INVALID_VERSION + 1;
    const static int EXCEPTION_MESSAGE_INVALID_TYPE = EXCEPTION_MESSAGE_INCOMPLETE_HEADER + 1;
    const static int EXCEPTION_MESSAGE_BAD_FORMAT = EXCEPTION_MESSAGE_INVALID_TYPE + 1;
    const static int EXCEPTION_MESSAGE_BAD_CAST = EXCEPTION_MESSAGE_BAD_FORMAT + 1;
};

class FastBreak final: public Exception{
    private:
    int reason_extra;

    public:
    explicit FastBreak(int extra): Exception(EXCEPTION_FASTBREAK), reason_extra(extra){}
    ~FastBreak(){}

};

class FastContinue final: public Exception{
    private:
    int reason_extra;

    public:
    explicit FastContinue(int extra): Exception(EXCEPTION_FASTCONTINUE), reason_extra(extra){}
    ~FastContinue(){}
    
};

void DebugFailFast();

#ifdef DEBUG_FAILFAST
#define DebugThrowElseReturnVoid(flag) \
{ \
    bool temp = (flag); \
    if(!temp){ \
        throw Exception(Exception::EXCEPTION_DEBUG_FAILFAST); \
    } \
}
    

#define DebugThrowElseReturn(flag, res) \
{ \
    bool temp = (flag); \
    if(!temp){ \
        throw Exception(Exception::EXCEPTION_DEBUG_FAILFAST); \
    } \
}

#define DebugThrow(flag) \
{ \
    bool temp = (flag); \
    if(!temp){ \
        throw Exception(Exception::EXCEPTION_DEBUG_FAILFAST); \
    } \
}

#else
#define DebugThrowElseReturnVoid(flag) \
{ \
    bool temp = (flag); \
    if(!temp){ \
        return; \
    } \
}

#define DebugThrowElseReturn(flag, res) \
{ \
    bool temp = (flag); \
    if(!temp){ \
        return res; \
    } \
}

#define DebugThrow(flag) 

#endif // DEBUG_FAILFAST
#endif