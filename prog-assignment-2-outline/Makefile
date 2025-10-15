# schedlab/Makefile
BPF_CLANG   ?= clang
BPF_CFLAGS  := -O2 -g -target bpf -D__TARGET_BPF__ -Wall -Werror

LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf)
LIBBPF_LIBS   := $(shell pkg-config --libs   libbpf)

all: schedlab

vmlinux.h:
	@echo "[-] Generating vmlinux.h from kernel BTF…"
	@bpftool btf dump file /sys/kernel/btf/vmlinux format c > $@

schedlab.bpf.o: schedlab.bpf.c vmlinux.h
	$(BPF_CLANG) $(BPF_CFLAGS) -I. -c $< -o $@

schedlab.skel.h: schedlab.bpf.o
	@echo "[-] Generating skeleton…"
	@bpftool gen skeleton $< > $@

schedlab: schedlab_user.c schedlab.skel.h
	$(CC) -O2 -g $< -o $@ $(LIBBPF_CFLAGS) $(LIBBPF_LIBS)

clean:
	rm -f vmlinux.h schedlab.bpf.o schedlab.skel.h schedlab

.PHONY: all clean
