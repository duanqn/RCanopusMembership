.PHONY: all tests

all:

tests: util_test

util_test: util_test.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@