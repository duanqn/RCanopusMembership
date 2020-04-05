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

    const static int EXCEPTION_FASTBREAK = 1;
    const static int EXCEPTION_FASTCONTINUE = EXCEPTION_FASTBREAK + 1;
    const static int EXCEPTION_INPUT_FORMAT = EXCEPTION_FASTCONTINUE + 1;
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
#endif