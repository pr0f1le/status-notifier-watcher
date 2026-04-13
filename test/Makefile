PKG_CONFIG := pkg-config
PKGS := dbus-1
INCS != $(PKG_CONFIG) --cflags $(PKGS)
LIBS != $(PKG_CONFIG) --libs $(PKGS)

DEBUG ?= 0
CFLAGS += -std=gnu99 $(INCS) -I../inc/
LDLIBS += $(LIBS)
V ?= 0

ifneq ($(filter 1 y yes true, $(DEBUG)),)
	CFLAGS += -g -DDEBUG
endif

ifneq ($(filter 1 y yes true, $(V)),)
	D =
else
	D = @
endif

DEPS := ../watcherd.c ../host.c ../shared.c
SRCS := snw.c snh.c single.c
TARGETS := $(SRCS:.c=)
OBJS := $(notdir $(DEPS:.c=.o)) $(SRCS:.c=.o)

TOTAL := $(words $(DEPS) $(SRCS))
CURRENT := 0

define compile
	$(eval CURRENT=$(shell echo $$(($(CURRENT)+1))))
	@printf "[%2d/%2d] Compiling $(1) ...\n" $(CURRENT) $(TOTAL)
	$(D)$(CC) $(CFLAGS) -c $(1) -o $(2)
endef

$(OBJS): $(SRCS) $(DEPS)

all: $(TARGETS)

%.o: ../%.c
	$(call compile,$<,$@)

%.o: %.c
	$(call compile,$<,$@)

%:%.o
	@printf "Linking $@ ...\n"
	$(D)$(CC) $(LDLIBS) -o $@ $^

snw: watcherd.o snw.o shared.o

snh: host.o snh.o shared.o

single: single.o watcherd.o host.o shared.o

clean:
	- rm $(TARGETS) $(OBJS)
