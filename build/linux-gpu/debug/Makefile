BASE=/usr/local/
C_PLATFORM= -pthread  -DSMINA_GPU
GPP=g++
C_OPTIONS= -O0  -g   
BOOST_LIB_VERSION=
NVCC=nvcc
NVOPTS=-O0 -g -G -lineinfo
OPENBABEL_INCLUDE=/usr/include/openbabel-2.0

include ../../makefile_common

.DEFAULT_GOAL := gpu

gpu: $(MAINOBJ) $(LIBOBJ)
	$(NVCC) $(NVOPTS) -gencode arch=compute_52,code=sm_52 $(LDFLAGS) -o smina.$@ $^ $(LIBS)  -lcudart
