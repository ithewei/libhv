MKDIR = mkdir -p
RM = rm -r
CP = cp -r

CFLAGS += -g -Wall -O3
ENABLE_SHARED=true
ifeq ($(ENABLE_SHARED),true)
	CFLAGS += -shared -fPIC
endif
CXXFLAGS += $(CFLAGS) -std=c++11
ARFLAGS := cr

INCDIR = include
LIBDIR = lib
SRCDIR = src
BINDIR = bin
DEPDIR = 3rd
CONFDIR = conf
DISTDIR = dist

TARGET = test
ifeq ($(OS),Windows_NT)
TARGET := $(addsuffix .exe, $(TARGET))
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
#LDFLAGS += -Wl,-Bstatic  -luv
#LDFLAGS += -Wl,-Bdynamic -lm -lz -lpthread

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
	$(CXX) $^ -o $(BINDIR)/$@ $(LDFLAGS)
	#$(CXX) $(CXXFLAGS) $^ -o $(LIBDIR)/$@ $(LDFLAGS)
	#$(AR)  $(ARFLAGS) $(LIBDIR)/$@ $^

clean:
	$(RM) $(OBJS)
	$(RM) $(BINDIR)
	$(RM) $(LIBDIR)

install:

uninstall:

dist:
	$(MKDIR) $(DISTDIR)
	$(CP) $(BINDIR) $(LIBDIR) $(DISTDIR)

.PHONY: default all prepare clean install uninstall dist

