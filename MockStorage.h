#ifndef PERSISTENT_STORAGE_H_
#define PERSISTENT_STORAGE_H_

#include "CycleStatus.h"
#include <set>

class MockStorage{
    protected:
    std::set<uint16_t> m_completedCycles;

    public:
    MockStorage(): m_completedCycles(){}

    virtual ~MockStorage(){}

    void store(uint16_t x){
        m_completedCycles.insert(x);
    }

    inline bool exist(uint16_t x){
        return m_completedCycles.find(x) != m_completedCycles.end();
    }
};

#endif