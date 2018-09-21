CC = gcc
CXX = g++
MKDIR = mkdir -p
RM = rm -r

CPPFLAGS +=
CFLAGS +=
CXXFLAGS += -g -Wall -O3 -std=c++11

INCDIR = include
SRCDIR = src
LIBDIR = lib
BINDIR = bin

TARGET = test
ifeq ($(OS),Windows_NT)
TARGET := $(addsuffix .exe, $(TARGET))
endif

INCFLAGS += -I.
INCFLAGS += $(addprefix -I, $(INCDIR))
CXXFLAGS += $(INCFLAGS)

LDFLAGS += $(addprefix -L, $(LIBDIR))
LDFLAGS += -lpthread

DIRS += . $(SRCDIR) test
SRCS += $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c $(dir)/*.cc $(dir)/*.cpp))
#OBJS := $(patsubst %.cpp, %.o, $(SRCS))
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(info DIRS=$(DIRS))
$(info SRCS=$(SRCS))
$(info OBJS=$(OBJS))

all: prepare $(TARGET)

prepare:
	$(MKDIR) $(BINDIR)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ -o $(BINDIR)/$@ $(LDFLAGS)

clean:
	$(RM) $(OBJS)
	$(RM) $(BINDIR)