
RM:=/bin/rm -vrf
CC:=gcc
CXX:=g++
WARN:=-Wall -fstack-protector -Wformat
DBG:=-g
OPTS:=-O4 -march=native -flto -pipe -std=gnu99
DEFINITIONS:=FORTIFY_SOURCE=2 __USE_GNU
INCLUDEDIRS:=
LINK:=gcc
LIBDIRS:=
LIBNAMES:=pthread

TARGETS:=multiserver multiclient

ALLTARGETS:=$(TARGETS) sender listener

SENDEROBJS:=sender.o

LISTENEROBJS:=listener.o

MULTISERVEROBJS:=get_socket.o multiserver.o

MULTICLIENTOBJS:=get_socket.o multiclient.o

INCLUDES:=$(patsubst %,-I%,$(INCLUDEDIRS))
DEFINES:=$(patsubst %,-D%,$(DEFINITIONS))

LIBFLAGS:=$(WARN) $(OPTS) $(patsubst %,-L%,$(LIBDIRS)) $(patsubst %,-l%,$(LIBNAMES)) $(LDFLAGS)
COMPILEFLAGS:=$(WARN) $(OPTS) $(DBG) $(DEFINES) $(INCLUDES) $(CFLAGS)

default: $(TARGETS)

all: $(ALLTARGETS)

MULTISERVERDEPS:=$(patsubst %.o,.d/%.d,$(MULTISERVEROBJS))
MULTICLIENTDEPS:=$(patsubst %.o,.d/%.d,$(MULTICLIENTOBJS))

multiserver: $(MULTISERVERDEPS) $(MULTISERVEROBJS)
	$(LINK) $(LIBFLAGS) $(MULTISERVEROBJS) -o $@

multiclient: $(MULTICLIENTDEPS) $(MULTICLIENTOBJS)
	$(LINK) $(LIBFLAGS) $(MULTICLIENTOBJS) -o $@

sender: $(SENDEROBJS)
	$(LINK) $(LIBFLAGS) $(SENDEROBJS) -o $@

listener: $(LISTENEROBJS)
	$(LINK) $(LIBFLAGS) $(LISTENEROBJS) -o $@

%.o: %.c
	$(CC) $(COMPILEFLAGS) -c $<

.d/%.d: %.c
	@mkdir -p .d
	@gcc -MM $< > $@

.PHONY: clean

clean:
	$(RM) $(ALLTARGETS) $(SENDEROBJS) $(LISTENEROBJS) $(MULTISERVEROBJS) $(MULTICLIENTOBJS)

.PHONY: distclean

distclean: clean
	$(RM) .d ./.*.swp ./*~ tags

-include $(MULTISERVERDEPS)
-include $(MULTICLIENTDEPS)
