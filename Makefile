DEBUG=TRUE
CXXFLAGS += -std=c++17
LDFLAGS += -lpthread

TARGETS = membership_server

ifeq ($(DEBUG),TRUE)
	CXXFLAGS += -Og -g -DDEBUG_FAILFAST -DDEBUG_PRINT
else
	CXXFLAGS += -O2
endif

.PHONY: all tests clean

all: clean tests $(TARGETS)

membership_server: server.o config.o PeerConnection.o Peer.o ConnManager.o message.o exception.o
	$(CXX) $^ -o $@ $(LDFLAGS)

tests: util_test queue_test

queue_test: queue_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)
	
util_test: util_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@

clean:
	rm -f *_test
	rm -f *.o
	rm -f $(TARGETS)
