DEBUG=FALSE
CXXFLAGS += -std=c++17
LDFLAGS += -lpthread

TARGETS = server

ifeq ($(DEBUG),TRUE)
	CXXFLAGS += -Og -g
else
	CXXFLAGS += -O2
endif

.PHONY: all tests clean

all: clean tests $(TARGETS)

server: server.o config.o PeerConnection.o Peer.o
	$(CXX) $^ -o $@ $(LDFLAGS)

tests: util_test

util_test: util_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@

clean:
	rm -f *_test
	rm -f *.o
	rm -f $(TARGETS)
