#+++++++++++++++++++++++++++++++++configure++++++++++++++++++++++++++++++++++++++++
# OS=Windows,Linux,Android
# ARCH=x86,x86_64,arm,aarch64
# CC
# CXX
# CPPFLAGS += $(addprefix -D, $(DEFINES))
# CPPFLAGS += $(addprefix -I, $(INCDIRS))
# CFLAGS
# CXXFLAGS
# LDFLAGS += $(addprefix -L, $(LIBDIRS))
# LDFLAGS += $(addprefix -l, $(LIBS))
# BUILD_SHARED=true,false
#
# Usage:
# make all CROSS_COMPILE=arm-linux-androideabi- BUILD_SHARED=true OS=Android ARCH=arm DEFINES=USE_OPENCV
# DIRS=src LIBDIRS=3rd/lib/arm-linux-android LIBS="opencv_core opencv_highgui"
#+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

CC 	= $(CROSS_COMPILE)gcc
CC_TARGET=$(shell $(CC) -v 2>&1 | grep Target | sed 's/Target: //')
CXX = $(CROSS_COMPILE)g++
CPP = $(CC) -E
AS 	= $(CROSS_COMPILE)as
LD	= $(CROSS_COMPILE)ld
AR	= $(CROSS_COMPILE)ar
NM	= $(CROSS_COMPILE)nm
STRIP 	= $(CROSS_COMPILE)strip
OBJCOPY	= $(CROSS_COMPILE)objcopy
OBJDUMP	= $(CROSS_COMPILE)objdump

MKDIR = mkdir -p
RM = rm -r
CP = cp -r

ifeq ($(OS), Windows_NT)
	OS=Windows
endif

ifneq ($(findstring mingw, $(CC)), )
	OS=Windows
endif

ifneq ($(findstring android, $(CC)), )
	OS=Android
endif

ifndef OS
	OS=Linux
endif

CPPFLAGS += $(addprefix -D, $(DEFINES))
ifeq ($(OS), Windows)
	CPPFLAGS += -D_WIN32_WINNT=0x600
ifeq ($(BUILD_SHARED),true)
	CPPFLAGS += -DDLL_EXPORTS
endif
endif

COMMON_CFLAGS += -g -Wall -O3
ifeq ($(BUILD_SHARED),true)
	COMMON_CFLAGS += -shared -fPIC -fvisibility=hidden
endif
CFLAGS += $(COMMON_CFLAGS) -std=c99
CXXFLAGS += $(COMMON_CFLAGS) -std=c++11
ARFLAGS := cr

INCDIR = include
LIBDIR = lib
SRCDIR = src
BINDIR = bin
DEPDIR = 3rd
CONFDIR = etc
DISTDIR = dist
DOCDIR  = doc

TARGET = test

DIRS += $(shell find $(SRCDIR) -type d)
SRCS += $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c $(dir)/*.cc $(dir)/*.cpp))
ifeq ($(SRCS), )
	SRCS = $(wildcard *.c *.cc *.cpp)
endif
#OBJS := $(patsubst %.cpp, %.o, $(SRCS))
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(info TARGET=$(TARGET))
$(info DIRS=$(DIRS))
$(info SRCS=$(SRCS))
$(info OBJS=$(OBJS))

INCDIRS  += $(INCDIR) $(DEPDIR) $(DEPDIR)/include $(DIRS)
CPPFLAGS += $(addprefix -I, $(INCDIRS))

LIBDIRS += $(DEPDIR)/lib $(DEPDIR)/lib/$(CC_TARGET)
LDFLAGS += $(addprefix -L, $(LIBDIRS))
ifeq ($(OS), Windows)
	LDFLAGS += -static-libgcc -static-libstdc++
endif

#common LIBS
LDFLAGS += $(addprefix -l, $(LIBS))

ifeq ($(OS), Windows)
	LDFLAGS += -lwinmm -liphlpapi -lws2_32
	LDFLAGS += -Wl,-Bstatic -lstdc++ -lpthread -lm
else
ifeq ($(OS), Android)
	LDFLAGS += -llog -lm
else
	LDFLAGS += -lstdc++ -lpthread -lm -ldl
endif
endif

$(info OS=$(OS))
$(info ARCH=$(ARCH))
$(info MAKE=$(MAKE))
$(info CC=$(CC))
$(info CXX=$(CXX))
$(info $(shell $(CC) --version 2>&1 | head -n 1))
$(info CC_TARGET: $(CC_TARGET))
$(info CPPFLAGS=$(CPPFLAGS))
$(info CFLAGS=$(CFLAGS))
$(info CXXFLAGS=$(CXXFLAGS))
$(info LDFLAGS=$(LDFLAGS))

default: all

all: prepare $(TARGET)

prepare:
	$(MKDIR) $(BINDIR) $(LIBDIR)

$(TARGET): $(OBJS)
# executable
ifeq ($(OS), Windows)
	$(CXX) $(CXXFLAGS) $^ -o $(BINDIR)/$@.exe $(LDFLAGS)
else
	$(CXX) $(CXXFLAGS) $^ -o $(BINDIR)/$@ $(LDFLAGS)
endif
# dynamic
#ifeq ($(OS), Windows)
	#$(CXX) $(CXXFLAGS) $^ -o $(LIBDIR)/$@.dll $(LDFLAGS) -Wl,--output-def,$(LIBDIR)/$@.def
#else
	#$(CXX) $(CXXFLAGS) $^ -o $(LIBDIR)/$@.so $(LDFLAGS)
#endif
# archive
	#$(AR) $(ARFLAGS) $(LIBDIR)/$@.a $^

clean:
	$(RM) $(OBJS)
	$(RM) $(BINDIR)
	$(RM) $(LIBDIR)

install:

uninstall:

dist:
	$(MKDIR) $(DISTDIR)
	$(CP) $(INCDIR) $(LIBDIR) $(BINDIR) $(DISTDIR)

undist:
	$(RM) $(DISTDIR)

.PHONY: default all prepare clean install uninstall dist undist

