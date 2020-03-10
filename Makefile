DEBUG=FALSE
CXXFLAGS += -std=c++17
LDFLAGS += -lpthread

ifeq ($(DEBUG),TRUE)
	CXXFLAGS += -Og
else
	CXXFLAGS += -O2
endif

.PHONY: set_env all tests clean

all: set_env

tests: set_env util_test

util_test: util_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@

clean:
	rm *_test
	rm *.o
