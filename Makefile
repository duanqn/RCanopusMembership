DEBUG=FALSE
CXXFLAGS += -std=c++17
LDFLAGS += -lpthread

ifeq ($(DEBUG),TRUE)
	CXXFLAGS += -Og -g
else
	CXXFLAGS += -O2
endif

.PHONY: all tests clean

all: clean tests server

server: server.o config.o PeerConnection.o Peer.o
	$(CXX) $^ -o $@ $(LDFLAGS)

tests: util_test

util_test: util_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@

clean:
	rm *_test
	rm *.o
