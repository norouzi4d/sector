include ../Makefile.common

CCFLAGS += -I../common

ifeq ($(STATIC), 1)
   LDFLAGS += libsecurity.a ../lib/libcommon.a ../lib/libudt.a
else
   LDFLAGS += -L../lib -ludt -lcommon -lsecurity
endif

OBJS = security.o ssltransport.o

all: libsecurity.so libsecurity.a sslserver sslclient sserver ssl_cert_gen

%.o: %.cpp %.h
	$(C++) -fPIC $(CCFLAGS) $< -c

libsecurity.so: $(OBJS)
	$(C++) -shared -o $@ $^

libsecurity.a: $(OBJS)
	ar -rcs $@ $^

sserver: sserver.cpp
	$(C++) $^ -o $@ $(CCFLAGS) $(LDFLAGS)

sslserver: sslserver.cpp
	$(C++) $^ -o $@ $(CCFLAGS) $(LDFLAGS)

sslclient: sslclient.cpp
	$(C++) $^ -o $@ $(CCFLAGS) $(LDFLAGS)

ssl_cert_gen: ssl_cert_gen.cpp
	$(C++) $^ -o $@ -lssl -lstdc++

clean:
	rm -f *.o *.so *.a sserver sslserver sslclient ssl_cert_gen

install:
	mv libsecurity.so libsecurity.a ../lib
	./ssl_cert_gen security
