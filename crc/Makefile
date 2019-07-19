INC += -I.

SOURCES = $(foreach d,.,$(wildcard $(d)/*.c))
OBJS = $(patsubst %.c, %.o, $(SOURCES))
OBJS += crchash.o

CC += $(SHARED_FLAG)
all : libcrc.a
libcrc.a : $(OBJS)
	ar -rus $@ $^
	@echo ""
	@echo "+--------------------------------------------+"
	@echo "|            Finish compilation              |"
	@echo "+--------------------------------------------+"
	@echo "|         Thanks using libcrc.a              |"
	@echo "|   copyright(c)Wang Yaofu  voipman@qq.com   |"
	@echo "+--------------------------------------------+"

clean:
	rm -rf *.o *.a

