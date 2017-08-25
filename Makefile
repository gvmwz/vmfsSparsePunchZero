NAME := vmfsSparsePunchZero
CXX_SRCS := $(wildcard *.cpp)
CXX_OBJS := ${CXX_SRCS:.cpp=.o}

CXXFLAGS += -Wall -static -std=c++11 -g -O2
LDFLAGS += -Wl,--wrap,_dl_random -Wl,--defsym,__wrap__dl_random=_dl_argv

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
