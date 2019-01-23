# Windows,Linux,Android
ifeq ($(OS), Windows_NT)
	OS=Windows
else
	OS=Linux
endif
$(info OS=$(OS))

# x86,x86_64,arm,aarch64
#ARCH=x86
#$(info ARCH=$(ARCH))

ENABLE_SHARED=false
$(info ENABLE_SHARED=$(ENABLE_SHARED))

MKDIR = mkdir -p
RM = rm -r
CP = cp -r

CFLAGS += -g -Wall -O3
ifeq ($(ENABLE_SHARED),true)
	CFLAGS += -shared -fPIC -fvisibility=hidden
endif
CXXFLAGS += $(CFLAGS) -std=c++11
ARFLAGS := cr

INCDIR = include
LIBDIR = lib
SRCDIR = src
BINDIR = bin
DEPDIR = 3rd
CONFDIR = etc
DISTDIR = dist
DOCDIR = doc

TARGET = test
ifeq ($(OS), Windows)
CPPFLAGS += -D_WIN32_WINNT=0x600 -DDLL_EXPORTS
endif

DIRS += . test
DIRS += $(shell find $(SRCDIR) -type d)
SRCS += $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c $(dir)/*.cc $(dir)/*.cpp))
#OBJS := $(patsubst %.cpp, %.o, $(SRCS))
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(info TARGET=$(TARGET))
$(info DIRS=$(DIRS))
$(info SRCS=$(SRCS))
$(info OBJS=$(OBJS))

INCDIRS  += $(INCDIR) $(DEPDIR)/include $(DIRS)
CPPFLAGS += $(addprefix -I, $(INCDIRS))

LIBDIRS += $(LIBDIR) $(DEPDIR)/lib
LDFLAGS += $(addprefix -L, $(LIBDIRS))
LDFLAGS += -L3rd/lib/x86_64-linux-gnu
ifeq ($(OS), Windows)
	LDFLAGS += -static-libgcc -static-libstdc++
	LDFLAGS += -Wl,-Bstatic -lstdc++ -lpthread -lm
else
	LDFLAGS += -Wl,-Bstatic -luv
	LDFLAGS += -Wl,-Bdynamic -lstdc++ -lpthread -lm
endif

$(info MAKE=$(MAKE))
$(info CC=$(CC))
$(info CXX=$(CXX))
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

