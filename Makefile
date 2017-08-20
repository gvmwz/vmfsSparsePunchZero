NAME := vmfsSparsePunchZero
CXX_SRCS := $(wildcard *.cpp)
CXX_OBJS := ${CXX_SRCS:.cpp=.o}

ARCH := native

CXXFLAGS += -Wall -Wextra -static -std=c++11 -pthread -g -O3 -flto -march=$(ARCH) -mtune=$(ARCH)

all: $(NAME)

$(NAME): $(CXX_OBJS)
	$(CXX) -o $@ $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS)
	objcopy --only-keep-debug $(NAME) $(NAME).debug
	strip $(NAME)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

clean:
	$(RM) $(CXX_OBJS) $(NAME)
	$(RM) $(NAME).debug
