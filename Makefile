#+++++++++++++++++++++++++++++++++configure++++++++++++++++++++++++++++++++++++++++
# OS=Windows,Linux,Android
# ARCH=x86,x86_64,arm,aarch64
# CC  = $(CROSS_COMPILE)gcc
# CXX = $(CROSS_COMPILE)g++
# CPPFLAGS += $(addprefix -D, $(DEFINES))
# CPPFLAGS += $(addprefix -I, $(INCDIRS))
# LDFLAGS  += $(addprefix -L, $(LIBDIRS))
# LDFLAGS  += $(addprefix -l, $(LIBS))
#
# Usage:
# make all TARGET_TYPE=SHARED \
# CROSS_COMPILE=arm-linux-androideabi- \
# DEFINES=USE_OPENCV \
# LIBS="opencv_core opencv_highgui"
#+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#BUILD_TYPE=DEBUG,RELEASE
BUILD_TYPE=DEBUG
#TARGET_TYPE=EXECUTABLE,SHARED,STATIC
TARGET_TYPE=EXECUTABLE

CC 	= $(CROSS_COMPILE)gcc
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

TARGET_PLATFORM=$(shell $(CC) -v 2>&1 | grep Target | sed 's/Target: //')
ifneq ($(findstring mingw, $(TARGET_PLATFORM)), )
	OS=Windows
endif
ifneq ($(findstring android, $(TARGET_PLATFORM)), )
	OS=Android
endif
ifndef OS
	OS=Linux
endif

ifndef ARCH
ARCH=$(shell echo 'x86_64-linux-gnu' | awk -F'-' '{print $$1}')
endif

ifeq ($(BUILD_TYPE), DEBUG)
	DEFAULT_CFLAGS = -g
endif
DEFAULT_CFLAGS += -Wall -O3 -fPIC
ifneq ($(TARGET_TYPE), EXECUTABLE)
	DEFAULT_CFLAGS += -shared -fvisibility=hidden
endif

ifndef CFLAGS
CFLAGS := $(DEFAULT_CFLAGS) -std=c99
endif
ifndef CXXFLAGS
CXXFLAGS := $(DEFAULT_CFLAGS) -std=c++11
endif
ifndef ARFLAGS
ARFLAGS := cr
endif

INCDIR = include
LIBDIR = lib
SRCDIR = src
BINDIR = bin
DEPDIR = 3rd
CONFDIR = etc
DISTDIR = dist
DOCDIR  = doc

SRCDIRS += $(shell find $(SRCDIR) -type d)
INCDIRS += $(INCDIR) $(DEPDIR) $(DEPDIR)/include $(SRCDIRS)
LIBDIRS += $(LIBDIR) $(DEPDIR)/lib $(DEPDIR)/lib/$(TARGET_PLATFORM)

CPPFLAGS += $(addprefix -D, $(DEFINES))
ifeq ($(OS), Windows)
	CPPFLAGS += -D_WIN32_WINNT=0x600
ifeq ($(BUILD_SHARED), true)
	CPPFLAGS += -DDLL_EXPORTS
endif
endif
CPPFLAGS += $(addprefix -I, $(INCDIRS))

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

$(info $(shell $(CC) --version 2>&1 | head -n 1))
$(info TARGET_PLATFORM=$(TARGET_PLATFORM))
$(info OS=$(OS))
$(info ARCH=$(ARCH))
$(info MAKE=$(MAKE))
$(info CC=$(CC))
$(info CXX=$(CXX))
$(info CPPFLAGS=$(CPPFLAGS))
$(info CFLAGS=$(CFLAGS))
$(info CXXFLAGS=$(CXXFLAGS))
$(info LDFLAGS=$(LDFLAGS))

TARGET_NAME = test

SRCS += $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c $(dir)/*.cc $(dir)/*.cpp))
ifeq ($(SRCS), )
	SRCS = $(wildcard *.c *.cc *.cpp)
endif
#OBJS += $(patsubst %.c, %.o, $(SRCS))
#OBJS += $(patsubst %.cc, %.o, $(SRCS))
#OBJS += $(patsubst %.cpp, %.o, $(SRCS))
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(info TARGET_TYPE=$(TARGET_TYPE))
$(info TARGET_NAME=$(TARGET_NAME))
$(info SRCS=$(SRCS))
$(info OBJS=$(OBJS))

default: all

all: prepare $(TARGET_NAME)

prepare:
	$(MKDIR) $(BINDIR) $(LIBDIR)

$(TARGET_NAME): $(OBJS)
ifeq ($(TARGET_TYPE), SHARED)
ifeq ($(OS), Windows)
	$(CXX) $(CXXFLAGS) $^ -o $(LIBDIR)/$@.dll $(LDFLAGS) -Wl,--output-def,$(LIBDIR)/$(@).def
else
	$(CXX) $(CXXFLAGS) $^ -o $(LIBDIR)/$@.so $(LDFLAGS)
endif
else
ifeq ($(TARGET_TYPE), STATIC)
	$(AR) $(ARFLAGS) $(LIBDIR)/$@.a $^
else
ifeq ($(OS), Windows)
	$(CXX) $(CXXFLAGS) $^ -o $(BINDIR)/$@.exe $(LDFLAGS)
else
	$(CXX) $(CXXFLAGS) $^ -o $(BINDIR)/$@ $(LDFLAGS)
endif
endif
endif

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

