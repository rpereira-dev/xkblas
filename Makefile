##
## Copyright 2009-2013,2018,2019 INRIA
##
## Contributors :
##
## thierry.gautier@inrialpes.fr
##
## This software is a computer program whose purpose is to execute
## blas subroutines on multi-GPUs system.
##
## This software is governed by the CeCILL-C license under French law and
## abiding by the rules of distribution of free software.  You can  use,
## modify and/ or redistribute the software under the terms of the CeCILL-C
## license as circulated by CEA, CNRS and INRIA at the following URL
## "http://www.cecill.info".

## As a counterpart to the access to the source code and  rights to copy,
## modify and redistribute granted by the license, users are provided only
## with a limited warranty  and the software's author,  the holder of the
## economic rights,  and the successive licensors  have only  limited
## liability.

## In this respect, the user's attention is drawn to the risks associated
## with loading,  using,  modifying and/or developing or reproducing the
## software by the user in light of its specific status of free software,
## that may mean  that it is complicated to manipulate,  and  that  also
## therefore means  that it is reserved for developers  and  experienced
## professionals having in-depth computer knowledge. Users are therefore
## encouraged to load and test the software's suitability as regards their
## requirements in conditions enabling the security of their systems and/or
## data to be ensured and,  more generally, to use and operate it in the
## same conditions as regards security.

## The fact that you are presently reading this means that you have had
## knowledge of the CeCILL-C license and that you accept its terms.
##
#
include make.inc

# Default: do not use dynamic loaded driver for accelerator
KAAPI_USE_DYNLOADER=0
GIT_HASH=$(shell (git describe --always --tags --long --abbrev=16 || cat kaapi_version.h |cut -d\  -f3) 2>/dev/null)

KAAPIDIR=./kaapi
BLASDIR=./blas

#
# Common C flags and libs
#
CPPFLAGS=-Ikaapi -Iblas -Itrace
LDFLAGS=

ifeq ($(KAAPI_USE_DYNLOADER),0)
  $(info Configure with plugin statically linked into libkaapi)
else
  $(info Configure with dynamic loaded plugin)
endif

ifeq ($(KAAPI_USE_PERFCOUNTER),0)
  $(info Configure without perfcounter support)
  PERFCTR_FLAGS=-DKAAPI_USE_PERFCOUNTER=0
else
  $(info Configure with perfcounter support)
  PERFCTR_FLAGS=-DKAAPI_USE_PERFCOUNTER=1
endif

ifeq ($(KAAPI_USE_TRACELIB),0)
  $(info Configure without tracing facility)
  TRACELIB_FLAGS=-DKAAPI_USE_TRACELIB=0
else
  $(info Configure with tracing facility)
  TRACELIB_FLAGS=-DKAAPI_USE_TRACELIB=1
endif

ifdef HWLOC_HOME
#  $(info  "$$(HWLOC_HOME)=$(HWLOC_HOME)  is defined - use HWLOC")
  HWLOC_FLAGS=-I${HWLOC_HOME}/include -DKAAPI_USE_HWLOC=1
  HWLOC_LIBS=-Wl,-rpath=${HWLOC_HOME}/lib -L${HWLOC_HOME}/lib -lhwloc
else
#  $(info  "$$(HWLOC_HOME)  is not defined - do not use HWLOC")
  HWLOC_FLAGS=-DKAAPI_USE_HWLOC=0
endif

ifdef KAAPI_USE_GPU_CUDA
  ifeq ($(KAAPI_USE_GPU_CUDA),1)
    #use the implementation on top of the driver API
    CUDA_FLAGS=-I${CUDA_HOME}/include -DKAAPI_USE_CUDA=1 -DKAAPI_USE_CUDA_DRIVER_API=1
    CUDA_LIBS=-Wl,-rpath=${CUDA_HOME}/lib64 -L${CUDA_HOME}/lib64 -lcublas -lcuda -lcudart
    $(info CUDA defined and used through the driver API)
  endif
  ifeq ($(KAAPI_USE_GPU_CUDA),2)
    #use the implementation on top of the runtime API
    CUDA_FLAGS=-I${CUDA_HOME}/include -DKAAPI_USE_CUDA=1 -DKAAPI_USE_CUDA_RUNTIME_API=1
    CUDA_LIBS=-Wl,-rpath=${CUDA_HOME}/lib64 -L${CUDA_HOME}/lib64 -lcublas -lcudart
    $(info CUDA defined and used through the runtime API)
  else
    $(info "KAAPI_USE_GPU_CUDA defined to : $(KAAPI_USE_GPU_CUDA)")
  endif
  CUDA_KAAPI_PLUGIN=libkaapi_plugin_cuda.so.1
  CUDA_KAAPI_PLUGIN_C=${KAAPIDIR}/kaapi_plugin_cuda.c
else
  $(info $$(KAAPI_USE_GPU_CUDA) is not defined - do not use CUDA)
endif

ifdef KAAPI_USE_GPU_HIP
  HIPCC=hipcc -Wunused-command-line-argument
  HIP_FLAGS=-D__HIP_PLATFORM_AMD__=1 -I${HIP_HOME}/include -I${HIPBLAS_HOME}/include -I${ROCBLAS_HOME}/include -DKAAPI_USE_CUDA=1 -DKAAPI_USE_HIP=1
  #we use rocm/hip implementation:
  HIP_LIBS=-Wl,-rpath=${HIP_HOME}/lib -L${HIP_HOME}/lib -L${HIPBLAS_HOME}/lib -lhipblas
  HIP_KAAPI_PLUGIN=libkaapi_plugin_hip.so.1
  HIP_KAAPI_PLUGIN_C=${KAAPIDIR}/kaapi_plugin_hip.c
  $(info HIP defined and used)
else
  HIP_FLAGS=-DKAAPI_USE_HIP=0
  $(info $$(KAAPI_USE_GPU_HIP) is not defined - do not use HIP)
endif

CPPFLAGS=${HWLOC_FLAGS} ${CUDA_FLAGS} ${HIP_FLAGS} ${TRACELIB_FLAGS} ${PERFCTR_FLAGS}
LDFLAGS=${HWLOC_LIBS} ${CUDA_LIBS} ${HIP_LIBS}

$(info Using $(BLAS_LIB_SO) with flags: $(BLAS_CPPFLAGS))


#
# UKAAPI sub library
#
UKAAPI_LIBNAME=libkaapi.so
UKAAPI_LIBNAME_A=libkaapi.a
UKAAPI_SRC=${KAAPIDIR}/kaapi_format.c ${KAAPIDIR}/kaapi_impl.c  ${KAAPIDIR}/kaapi_task.c ${KAAPIDIR}/kaapi_rt.c  ${KAAPIDIR}/kaapi_hashmap.c ${KAAPIDIR}/kaapi_barrier.c ${KAAPIDIR}/kaapi_memory.c ${KAAPIDIR}/kaapi_offload_stream.c ${KAAPIDIR}/kaapi_offload.c ${KAAPIDIR}/kaapi_offload_device.c  ${KAAPIDIR}/kaapi_ld.c ${KAAPIDIR}/kaapi_dbg.c
UKAAPI_FILE_LIB=${UKAAPI_SRC} ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload_stream.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_offload_dbg.h ${KAAPIDIR}/kaapi_plugin.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_format.h ${KAAPIDIR}/kaapi_hashmap.h ${KAAPIDIR}/kaapi_memory.h  kaapi_version.h

ifeq ($(KAAPI_USE_TRACELIB),1)
UKAAPI_SRC+=${KAAPIDIR}/kaapi_trace_lib.c ${KAAPIDIR}/kaapi_trace_parser.c ${KAAPIDIR}/kaapi_trace_recorder.c
UKAAPI_FILE_LIB+=${KAAPIDIR}/kaapi_trace.h ${KAAPIDIR}/kaapi_trace_recorder.h
endif

UKAAPI_SRC_PLUGIN=${CUDA_KAAPI_PLUGIN_C} ${HIP_KAAPI_PLUGIN_C} ${KAAPIDIR}/kaapi_plugin_host.c
UKAAPI_FILE_PLUGIN=${UKAAPI_SRC_PLUGIN} ${KAAPIDIR}/kaapi_plugin.h

ifeq ($(KAAPI_USE_DYNLOADER),0)
  # merge FILE_PLUGIN in normal library
  UKAAPI_FILE_LIB+=$(UKAAPI_FILE_PLUGIN)
  UKAAPI_TARGET_PLUGIN=
  UKAAPI_LDFLAGS=${LDFLAGS} 
else
  UKAAPI_LDFLAGS="-ldl"
  UKAAPI_TARGET_PLUGIN=libkaapi_plugin_host.so.1 ${CUDA_KAAPI_PLUGIN} ${HIP_KAAPI_PLUGIN}
endif


#
# LIBXKBLAS
#
XKBLAS_BLAS_PRECISION_s=\
  ${BLASDIR}/sgemm.c \
  ${BLASDIR}/sgemmt.c \
  ${BLASDIR}/strsm.c \
  ${BLASDIR}/strmm.c \
  ${BLASDIR}/ssymm.c \
  ${BLASDIR}/ssyrk.c \
  ${BLASDIR}/ssyr2k.c \
  ${BLASDIR}/sswap.c \
  ${BLASDIR}/xkblas_s.h\
  ${BLASDIR}/xkblas_f77_s.h

XKBLAS_BLAS_PRECISION_d=\
  ${BLASDIR}/dgemm.c\
  ${BLASDIR}/dgemmt.c\
  ${BLASDIR}/dtrsm.c\
  ${BLASDIR}/dtrmm.c\
  ${BLASDIR}/dsymm.c\
  ${BLASDIR}/dsyrk.c\
  ${BLASDIR}/dsyr2k.c\
  ${BLASDIR}/dswap.c\
  ${BLASDIR}/xkblas_d.h\
  ${BLASDIR}/xkblas_f77_d.h

XKBLAS_BLAS_PRECISION_c=\
  ${BLASDIR}/cgemm.c\
  ${BLASDIR}/cgemmt.c\
  ${BLASDIR}/ctrsm.c\
  ${BLASDIR}/ctrmm.c\
  ${BLASDIR}/csymm.c\
  ${BLASDIR}/csyrk.c\
  ${BLASDIR}/csyr2k.c\
  ${BLASDIR}/chemm.c \
  ${BLASDIR}/cherk.c \
  ${BLASDIR}/cher2k.c \
  ${BLASDIR}/cswap.c \
  ${BLASDIR}/xkblas_c.h\
  ${BLASDIR}/xkblas_f77_c.h

XKBLAS_BLAS_PRECISION_z=\
  ${BLASDIR}/zgemm.c\
  ${BLASDIR}/zgemmt.c\
  ${BLASDIR}/ztrsm.c\
  ${BLASDIR}/ztrmm.c\
  ${BLASDIR}/zsymm.c\
  ${BLASDIR}/zsyrk.c\
  ${BLASDIR}/zsyr2k.c\
  ${BLASDIR}/zhemm.c \
  ${BLASDIR}/zherk.c \
  ${BLASDIR}/zher2k.c \
  ${BLASDIR}/zswap.c \
  ${BLASDIR}/xkblas_z.h\
  ${BLASDIR}/xkblas_f77_z.h

XKBLAS_GEN_BLAS=${BLASDIR}/internal_register.h\
  ${XKBLAS_BLAS_PRECISION_s}\
  ${XKBLAS_BLAS_PRECISION_d}\
  ${XKBLAS_BLAS_PRECISION_c}

XKBLAS_BLAS=\
  ${XKBLAS_BLAS_PRECISION_z}\
  ${XKBLAS_GEN_BLAS}

XKBLAS_TASK_PRECISION_s=\
  ${BLASDIR}/task_sgemm.c\
  ${BLASDIR}/task_ssymm.c\
  ${BLASDIR}/task_sgemmt.c\
  ${BLASDIR}/task_strsm.c\
  ${BLASDIR}/task_strmm.c\
  ${BLASDIR}/task_ssyrk.c\
  ${BLASDIR}/task_ssyr2k.c\
  ${BLASDIR}/task_sswap.c\
  ${BLASDIR}/task_s.h \
  ${BLASDIR}/task_s_internal.h

XKBLAS_TASK_PRECISION_d=\
  ${BLASDIR}/task_dgemm.c\
  ${BLASDIR}/task_dsymm.c\
  ${BLASDIR}/task_dgemmt.c\
  ${BLASDIR}/task_dtrsm.c\
  ${BLASDIR}/task_dtrmm.c\
  ${BLASDIR}/task_dsyrk.c\
  ${BLASDIR}/task_dsyr2k.c\
  ${BLASDIR}/task_dswap.c\
  ${BLASDIR}/task_d.h \
  ${BLASDIR}/task_d_internal.h

XKBLAS_TASK_PRECISION_c=\
  ${BLASDIR}/task_cgemm.c\
  ${BLASDIR}/task_csymm.c\
  ${BLASDIR}/task_cgemmt.c\
  ${BLASDIR}/task_ctrsm.c\
  ${BLASDIR}/task_ctrmm.c\
  ${BLASDIR}/task_csyrk.c\
  ${BLASDIR}/task_csyr2k.c\
  ${BLASDIR}/task_chemm.c\
  ${BLASDIR}/task_cherk.c\
  ${BLASDIR}/task_cher2k.c\
  ${BLASDIR}/task_cswap.c\
  ${BLASDIR}/task_c.h\
  ${BLASDIR}/task_c_internal.h

XKBLAS_TASK_PRECISION_z=\
  ${BLASDIR}/task_zgemm.c\
  ${BLASDIR}/task_zsymm.c\
  ${BLASDIR}/task_zgemmt.c\
  ${BLASDIR}/task_ztrsm.c\
  ${BLASDIR}/task_ztrmm.c\
  ${BLASDIR}/task_zsyrk.c\
  ${BLASDIR}/task_zsyr2k.c\
  ${BLASDIR}/task_zhemm.c\
  ${BLASDIR}/task_zherk.c\
  ${BLASDIR}/task_zher2k.c\
  ${BLASDIR}/task_zswap.c\
  ${BLASDIR}/task_z.h\
  ${BLASDIR}/task_z_internal.h

XKBLAS_GEN_TASK=\
  ${XKBLAS_TASK_PRECISION_s}\
  ${XKBLAS_TASK_PRECISION_d}\
  ${XKBLAS_TASK_PRECISION_c}

XKBLAS_TASK=\
  ${XKBLAS_TASK_PRECISION_z}\
  ${XKBLAS_GEN_TASK}


#
# LIBXKBLAS_WRAPPER
#
XKBLAS_WRAPPER_SRC=${BLASDIR}/libxkblas_wrapper.c
XKBLAS_WRAPPER_PRECISION_z=${BLASDIR}/libxkblas_wrapper_z.c
XKBLAS_WRAPPER_PRECISION_c=${BLASDIR}/libxkblas_wrapper_c.c
XKBLAS_WRAPPER_PRECISION_d=${BLASDIR}/libxkblas_wrapper_d.c
XKBLAS_WRAPPER_PRECISION_s=${BLASDIR}/libxkblas_wrapper_s.c
XKBLAS_WRAPPER_PRECISION=${XKBLAS_WRAPPER_PRECISION_z} ${XKBLAS_WRAPPER_PRECISION_c} ${XKBLAS_WRAPPER_PRECISION_d} ${XKBLAS_WRAPPER_PRECISION_s}
XKBLAS_WRAPPER_GENFILES=${BLASDIR}/libxkblas_wrapper_c.c ${BLASDIR}/libxkblas_wrapper_d.c ${BLASDIR}/libxkblas_wrapper_s.c
XKBLAS_WRAPPER_LDFLAGS=${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas
XKBLAS_WRAPPER_CPPFLAGS=${CPPFLAGS}


#
# Testing files
#
XKBLAS_TESTING_PRECISION_s=\
  testing/testing_sgemm.c\
  testing/testing_strsm.c\
  testing/testing_strmm.c\
  testing/testing_ssymm.c\
  testing/testing_ssyrk.c\
  testing/testing_ssyr2k.c\
  testing/splgsy.c\
  testing/testing_sauxiliary.h testing/testing_sauxiliary.c

XKBLAS_TESTING_PRECISION_d=\
  testing/testing_dgemm.c\
  testing/testing_dtrsm.c\
  testing/testing_dtrmm.c\
  testing/testing_dsymm.c\
  testing/testing_dsyrk.c\
  testing/testing_dsyr2k.c\
  testing/dplgsy.c\
  testing/testing_dauxiliary.h testing/testing_dauxiliary.c

XKBLAS_TESTING_PRECISION_c=\
  testing/testing_cgemm.c\
  testing/testing_ctrsm.c\
  testing/testing_ctrmm.c\
  testing/testing_csymm.c\
  testing/testing_csyrk.c\
  testing/testing_csyr2k.c\
  testing/testing_chemm.c\
  testing/testing_cherk.c\
  testing/testing_cher2k.c\
  testing/cplgsy.c\
  testing/cplghe.c\
  testing/testing_cauxiliary.h testing/testing_cauxiliary.c

XKBLAS_TESTING_PRECISION_z=\
  testing/testing_zgemm.c\
  testing/testing_ztrsm.c\
  testing/testing_ztrmm.c\
  testing/testing_zsymm.c\
  testing/testing_zsyrk.c\
  testing/testing_zsyr2k.c\
  testing/testing_zhemm.c\
  testing/testing_zherk.c\
  testing/testing_zher2k.c\
  testing/zplgsy.c\
  testing/zplghe.c\
  testing/testing_zauxiliary.h testing/testing_zauxiliary.c

XKBLAS_GEN_TESTING= \
  ${XKBLAS_TESTING_PRECISION_s}\
  ${XKBLAS_TESTING_PRECISION_d}\
  ${XKBLAS_TESTING_PRECISION_c}

XKBLAS_TESTING= \
  testing/run_tests\
  ${XKBLAS_TESTING_PRECISION_z}\
  ${XKBLAS_GEN_TESTING}

XKBLAS_OTHER_FILES=\
  ${BLASDIR}/xkblas.h\
  ${BLASDIR}/common.h\
  ${BLASDIR}/xkblas.c\
  ${BLASDIR}/dbg_blas.c\

XKBLAS_FILES=\
  ${XKBLAS_OTHER_FILES}\
  ${XKBLAS_BLAS}\
  ${XKBLAS_TASK}
XKBLAS_SRC=$(filter %.c, ${XKBLAS_FILES})
XKBLAS_CPPFLAGS=-I. -Ikaapi -Itrace -Iblas -DXKBLAS_BLASLIB='"${BLAS_LIB_SO}"' ${BLAS_CPPFLAGS} ${CPPFLAGS}
XKBLAS_LDFLAGS=${UKAAPI_LDFLAGS} -lkaapi
#XKBLAS_LDFLAGS=${UKAAPI_LDFLAGS}
#-L${KAAPI_HOME} -lkaapi 


#
XKBLAS_ALL_GENFILES=${XKBLAS_GEN_BLAS} ${XKBLAS_GEN_TASK} ${XKBLAS_GEN_TESTING} ${XKBLAS_WRAPPER_GENFILES}

#
# File per precision
#
FILE_PRECISION_s=\
            ${XKBLAS_TESTING_PRECISION_s}\
            ${XKBLAS_TASK_PRECISION_s}\
            ${XKBLAS_BLAS_PRECISION_s}\
	    ${XKBLAS_WRAPPER_PRECISION_s}
FILE_PRECISION_d=\
            ${XKBLAS_TESTING_PRECISION_d}\
            ${XKBLAS_TASK_PRECISION_d}\
            ${XKBLAS_BLAS_PRECISION_d}\
	    ${XKBLAS_WRAPPER_PRECISION_d}
FILE_PRECISION_c=\
            ${XKBLAS_TESTING_PRECISION_c}\
            ${XKBLAS_TASK_PRECISION_c}\
            ${XKBLAS_BLAS_PRECISION_c}\
	    ${XKBLAS_WRAPPER_PRECISION_c}
FILE_PRECISION_z=\
            ${XKBLAS_TESTING_PRECISION_z}\
            ${XKBLAS_TASK_PRECISION_z}\
            ${XKBLAS_BLAS_PRECISION_z}\
	    ${XKBLAS_WRAPPER_PRECISION_z}

FILE_OTHER=${BLASDIR}/xkblas.h ${BLASDIR}/common.h\
  ${BLASDIR}/xkblas.c ${BLASDIR}/dbg_blas.c\
  ${BLASDIR}/libxkblas_wrapper.c\
  ${BLASDIR}/internal_register.h\
  ${UKAAPI_SRC}

ALL_FILE=\
  ${FILE_OTHER}\
  ${FILE_PRECISION_s}\
  ${FILE_PRECISION_d}\
  ${FILE_PRECISION_c}\
  ${FILE_PRECISION_z}


.PHONY: clean gitlastcommit


define todo_make

----------------
XKBLAS $(GIT_HASH) has to be compiled: Please enter either make all or make dynamic or make static. 
Then you can install the library into your repository with make install PREFIX="<your repository>"
Any question, suggestion or comment may be send to authors.
Please visit https://gitlab.inria.fr/xkblas.
----------------
endef
export todo_make


define todo_after_make

----------------
XKBLAS $(GIT_HASH) was compiled. To install library into your repository, enter make install PREFIX="<your repository>"
Any question, suggestion or comment may be send to authors.
Please visit https://gitlab.inria.fr/xkblas.
----------------
endef
export todo_after_make

whattodo: dynamic
	#@echo "$$todo_make"

all: alldeps dynamic 
	@echo "$$todo_after_make"

alldeps: .generated .generated_testing ${UKAAPI_LIBNAME} plugin 

dynamic: libxkblas.so libxkblas_blaswrapper.so 
static: libxkblas.a

testing: testing_z testing_d testing_c testing_s testing_z_wrapper testing_d_wrapper testing_c_wrapper testing_s_wrapper
	@echo "$$todo_after_make"

#BEGIN_DONOT_EXPORT
.generated: gen_precision.sh \
            ${XKBLAS_TASK_PRECISION_z}\
            ${XKBLAS_BLAS_PRECISION_z}\
            ${BLASDIR}/task_format.h 
	touch .generated
	(cd blas ; ../gen_precision.sh)

.generated_testing: .generated gen_precision.sh \
            ${XKBLAS_TESTING_PRECISION_z}
	touch .generated_testing
	(cd testing ; ../gen_precision.sh)

${XKBLAS_ALL_GENFILES}: .generated .generated_testing

${BLASDIR}/libxkblas_wrapper_c.c: gen_precision_one.sh ${BLASDIR}/libxkblas_wrapper_z.c
	(./gen_precision_one.sh c ${BLASDIR}/libxkblas_wrapper_z.c)
${BLASDIR}/libxkblas_wrapper_d.c: gen_precision_one.sh ${BLASDIR}/libxkblas_wrapper_z.c
	(./gen_precision_one.sh d ${BLASDIR}/libxkblas_wrapper_z.c)
${BLASDIR}/libxkblas_wrapper_s.c: gen_precision_one.sh ${BLASDIR}/libxkblas_wrapper_z.c
	(./gen_precision_one.sh s ${BLASDIR}/libxkblas_wrapper_z.c)

ifneq ('$(GIT_HASH)','')
gitlastcommit:
	@echo "#define GIT_HASH ${GIT_HASH}" > .kaapi_version.h
	@if cmp --quiet .kaapi_version.h ./kaapi_version.h ; then \
		echo "Git version '${GIT_HASH}' not changed" ;\
	$(RM) .kaapi_version.h ;\
	else \
		echo "New git version, updating kaapi_version.h" ;\
		mv .kaapi_version.h ./kaapi_version.h ; \
	fi
else
gitlastcommit:
endif
kaapi_version.h:  gitlastcommit

#DISTTAG=`git describe --tags  --abbrev=0`-${GIT_HASH}
DISTTAG=${GIT_HASH}
dist:  .generated .generated_testing
	mkdir -p xkblas/kaapi xkblas/trace xkblas/blas xkblas/testing
	cp ${UKAAPI_FILE_LIB} ${UKAAPI_FILE_PLUGIN} AUTHORS COPYING LICENCE README.md make.inc Makefile xkblas/kaapi
	cp ${BLASDIR}/libxkblas_wrapper.h ${BLASDIR}/flops.h ${BLASDIR}/xkblas_f77.h ${BLASDIR}/task_format.h ${BLASDIR}/xkblas_z.h ${BLASDIR}/xkblas_f77_z.h ${BLASDIR}/task_z.h ${BLASDIR}/task_z_internal.h ${XKBLAS_FILES} ${XKBLAS_WRAPPER_SRC}${XKBLAS_WRAPPER_PRECISION} xkblas/blas
	cp ${XKBLAS_TESTING} xkblas/testing
	echo ${GIT_HASH} > xkblas/version
	#update Makefile:
	perl -i -pe 'BEGIN{undef $$/;} s/^#BEGIN_DONOT_EXPORT.*^#END_DONOT_EXPORT//smg' xkblas/Makefile
	cat xkblas/Makefile|sed -e 's+.generated_testing++g; s+.generated++g' > xkblas/Make
	echo ${GIT_HASH} > xkblas/VERSION
	mv -f xkblas/Make xkblas/Makefile
	rm -f xkblas/._*
	tar cfvz xkblas-${DISTTAG}.tgz xkblas
	rm -rf xkblas
	@echo "Version: xkblas-${DISTTAG}.tgz"

distclean: clean
	rm -f ${XKBLAS_ALL_GENFILES} .generated

#END_DONOT_EXPORT

install:
	@if test -e libxkblas.a || test -e libxkblas.so -a -e libkaapi.so ; then \
		mkdir -p ${PREFIX}/include ${PREFIX}/lib ${PREFIX}/bin; \
		cp ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_atomic.h ${BLASDIR}/xkblas.h ${BLASDIR}/xkblas_?.h ${PREFIX}/include; \
		echo "XKBlas - Include files installed in ${PREFIX}/include";\
	        if test -e libxkblas.a ; then \
	  	  cp -f libxkblas.a ${PREFIX}/lib; \
		  echo "XKBlas - Static library libxklas.a installed in ${PREFIX}/lib"; \
        	fi;\
	        if test -e libxkblas.so -a -e libkaapi.so ; then \
	  	  cp -f ${TARGET_PLUGIN} libkaapi.so libxkblas.so libxkblas_blaswrapper.so ${PREFIX}/lib; \
		  echo "XKBlas - Dynamic libraries libkaapi.so libxklas.so libxkblas_blaswrapper.so installed in ${PREFIX}/lib"; \
 		fi;\
 		if test -e testing_z; then \
			cp testing_z ${PREFIX}/bin; \
                	echo "XKBlas - Testing Z programs installed in ${PREFIX}/bin";\
		fi;\
 		if test -e testing_c; then \
			cp testing_c ${PREFIX}/bin; \
                	echo "XKBlas - Testing C programs installed in ${PREFIX}/bin";\
		fi;\
 		if test -e testing_d; then \
			cp testing_d ${PREFIX}/bin; \
                	echo "XKBlas - Testing D programs installed in ${PREFIX}/bin";\
		fi;\
 		if test -e testing_s; then \
			cp testing_s ${PREFIX}/bin; \
                	echo "XKBlas - Testing S programs installed in ${PREFIX}/bin";\
		fi;\
	else \
		echo "Neither dynamic or static library is compiled"; \
	fi

plugin: ${TARGET_PLUGIN}


echo:
	echo ${GIT_HASH}

libkaapi.so: ${UKAAPI_FILE_LIB:.c=.o} ${UKAAPI_SRC_PLUGIN:.c=.o} Makefile
	$(CC) -shared -o libkaapi.so ${UKAAPI_SRC:%.c=%.o} ${UKAAPI_SRC_PLUGIN:.c=.o} ${UKAAPI_LDFLAGS}

libkaapi.a: ${UKAAPI_FILE_LIB:%.c=%_a.o} ${UKAAPI_SRC_PLUGIN:%.c=%_a.o} Makefile
	$(AR) crv libkaapi.a ${UKAAPI_SRC:%.c=%_a.o} ${UKAAPI_SRC_PLUGIN:%.c=%_a.o}
	$(RANLIB) libkaapi.a

libkaapi_plugin_host.so.1: ${KAAPIDIR}/kaapi_plugin_host.o libkaapi.so
	$(CC) -shared -o libkaapi_plugin_host.so.1 ${KAAPIDIR}/kaapi_plugin_host.o ${UKAAPI_LDFLAGS}

libkaapi_plugin_cuda.so.1: ${KAAPIDIR}/kaapi_plugin_cuda.o libkaapi.so
	$(CC) -shared -o libkaapi_plugin_cuda.so.1 ${KAAPIDIR}/kaapi_plugin_cuda.o ${UKAAPI_LDFLAGS} ${HWLOC_LIBS} ${CUDA_LIBS}

libkaapi_plugin_hip.so.1: ${KAAPIDIR}/kaapi_plugin_hip.o libkaapi.so
	$(CC) -shared -o libkaapi_plugin_hip.so.1 ${KAAPIDIR}/kaapi_plugin_hip.o ${UKAAPI_LDFLAGS} ${HWLOC_LIBS} ${HIP_LIBS}

ifdef KAAPI_USE_GPU_CUDA
libxkblas.so: libkaapi.so .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=.o} 
	echo ${XKBLAS_SRC:.c=.o}
	$(CC) -shared -o libxkblas.so ${XKBLAS_SRC:.c=.o} ${XKBLAS_LDFLAGS} ${XKBLAS_CPPFLAGS} -L${KAAPI_HOME} -lkaapi -lm

libxkblas.a: libkaapi.a .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=_a.o} 
	$(AR) crv libxkblas.a ${XKBLAS_SRC:.c=_a.o} ${UKAAPI_SRC:%.c=%_a.o} ${UKAAPI_SRC_PLUGIN:.c=_a.o} 
	$(RANLIB) libxkblas.a
endif
ifdef KAAPI_USE_GPU_HIP
libxkblas.so: libkaapi.so .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=.hip_o} 
	echo ${XKBLAS_SRC:.c=.hip_o}
	$(CC) -shared -o libxkblas.so ${XKBLAS_SRC:.c=.hip_o} ${XKBLAS_LDFLAGS} ${XKBLAS_CPPFLAGS} -L${KAAPI_HOME} -lkaapi -lm

libxkblas.a: libkaapi.a .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=.hip_a.o} 
	$(AR) crv libxkblas.a ${XKBLAS_SRC:.c=.hip_a.o} ${UKAAPI_SRC:%.c=%.hip_a.o} ${UKAAPI_SRC_PLUGIN:.c=.hip_a.o} 
	$(RANLIB) libxkblas.a
endif

ifdef KAAPI_USE_GPU_CUDA
${BLASDIR}/common.h: ${BLASDIR}/common.gen
	cp ${BLASDIR}/common.gen ${BLASDIR}/common.h
endif
ifdef KAAPI_USE_GPU_HIP
${BLASDIR}/common.h: ${BLASDIR}/common.gen
	hipify-perl ${BLASDIR}/common.gen > ${BLASDIR}/common.h
endif



${BLASDIR}/libxkblas_wrapper.c: ${BLASDIR}/libxkblas_wrapper_z.c ${BLASDIR}/libxkblas_wrapper_c.c ${BLASDIR}/libxkblas_wrapper_d.c ${BLASDIR}/libxkblas_wrapper_s.c
${BLASDIR}/libxkblas_wrapper.c: ${BLASDIR}/libxkblas_wrapper.h
${BLASDIR}/libxkblas_wrapper_z.c: ${BLASDIR}/libxkblas_wrapper.h
${BLASDIR}/libxkblas_wrapper_c.c: ${BLASDIR}/libxkblas_wrapper.h
${BLASDIR}/libxkblas_wrapper_d.c: ${BLASDIR}/libxkblas_wrapper.h
${BLASDIR}/libxkblas_wrapper_s.c: ${BLASDIR}/libxkblas_wrapper.h

libxkblas_blaswrapper.so: libxkblas.so ${XKBLAS_WRAPPER_SRC:.c=.o} ${XKBLAS_WRAPPER_PRECISION:.c=.o}
	$(CC) -shared -o libxkblas_blaswrapper.so ${XKBLAS_WRAPPER_SRC:.c=.o} ${XKBLAS_WRAPPER_PRECISION:.c=.o} ${XKBLAS_LDFLAGS} ${XKBLAS_CPPFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -ldl


testing_z: $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_z})) .generated_testing
	$(CC) -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c -o testing_z  -o $@ $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_z})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_z_wrapper: $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_z})) .generated_testing
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c -o testing_z  -o $@ $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_z})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_c: $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_c})) .generated_testing 
	$(CC) -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z -o testing_c  -o $@ $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_c})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_c_wrapper: $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_c})) .generated_testing 
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z -o testing_c  -o $@ $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_c})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_d: $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_d})) .generated_testing 
	$(CC) -DPRECISION_d -UPRECISION_s -UPRECISION_z -UPRECISION_c -o testing_d  -o $@ $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_d})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_d_wrapper: $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_d})) .generated_testing 
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_d -UPRECISION_s -UPRECISION_z -UPRECISION_c -o testing_d  -o $@ $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_d})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_s: $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_s})) .generated_testing 
	$(CC) -DPRECISION_s -UPRECISION_z -UPRECISION_d -UPRECISION_c -o testing_s  -o $@ $(patsubst %.c,%.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_s})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

testing_s_wrapper: $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_s})) .generated_testing 
	$(CC) -DTESTING_API_XKBLAS_WRAPPER  -DPRECISION_s -UPRECISION_z -UPRECISION_d -UPRECISION_c -o testing_s  -o $@ $(patsubst %.c,%_wrap.o, $(filter %.c, ${XKBLAS_TESTING_PRECISION_s})) ${XKBLAS_LDFLAGS} -L${KAAPI_HOME} -lxkblas -lkaapi -lm ${BLAS_LDFLAGS}

$(filter %.c,${XKBLAS_TESTING}): testing/testing_zauxiliary.h



# Dynamic lib
${KAAPIDIR}/kaapi_plugin_host.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_host.c ${KAAPIDIR}/kaapi_plugin.h  Makefile
	$(CC) -c -fPIC ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_host.c -o $@

${KAAPIDIR}/kaapi_plugin_cuda.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_cuda.c ${KAAPIDIR}/kaapi_plugin.h  Makefile
	$(CC) -c -fPIC ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_cuda.c -o $@

${KAAPIDIR}/kaapi_plugin_hip.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_hip.c ${KAAPIDIR}/kaapi_plugin.h  Makefile
	$(CC) -c -fPIC ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_hip.c -o $@

$(patsubst %.c,%.o,$(filter %.c,$(FILE_OTHER))): %.o:	 %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_z))): %.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_c))): %.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_d))): %.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_s))): %.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

#
%.hip.c:	 %.c
	hipify-perl $< |sed -e 's+hipComplex+hipblasComplex+g' \
		-e 's+hipDoubleComplex+hipblasDoubleComplex+g' \
		-e 's+cublasChemm+hipblasChemm+g' \
		-e 's+cublasCher2k+hipblasCher2k+g' \
		-e 's+cublasCherk+hipblasCherk+g' \
		-e 's+cublasCsymm+hipblasCsymm+g' \
		-e 's+cublasCsyr2k+hipblasCsyr2k+g' \
		-e 's+cublasCsyrk+hipblasCsyrk+g' \
		-e 's+cublasCtrmm+hipblasCtrmm+g' \
		-e 's+cublasCtrsm+hipblasCtrsm+g' \
		-e 's+cublasDsymm+hipblasDsymm+g' \
		-e 's+cublasDsyr2k+hipblasDsyr2k+g' \
		-e 's+cublasDsyrk+hipblasDsyrk+g' \
		-e 's+cublasDtrmm+hipblasDtrmm+g' \
		-e 's+cublasSsymm+hipblasSsymm+g' \
		-e 's+cublasSsyr2k+hipblasSsyr2k+g' \
		-e 's+cublasSsyrk+hipblasSsyrk+g' \
		-e 's+cublasStrmm+hipblasStrmm+g' \
		-e 's+cublasZhemm+hipblasZhemm+g' \
		-e 's+cublasZher2k+hipblasZher2k+g' \
		-e 's+cublasZherk+hipblasZherk+g' \
		-e 's+cublasZsymm+hipblasZsymm+g' \
		-e 's+cublasZsyr2k+hipblasZsyr2k+g' \
		-e 's+cublasZsyrk+hipblasZsyrk+g' \
		-e 's+cublasZtrmm+hipblasZtrmm+g' \
		-e 's+cublasZtrsm+hipblasZtrsm+g' \
		> $@

$(patsubst %.c,%.hip_o,$(filter %.c,$(FILE_OTHER))): %.hip_o:	 %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@

$(patsubst %.c,%.hip_o,$(filter %.c, $(FILE_PRECISION_z))): %.hip_o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_o,$(filter %.c, $(FILE_PRECISION_c))): %.hip_o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_o,$(filter %.c, $(FILE_PRECISION_d))): %.hip_o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_o,$(filter %.c, $(FILE_PRECISION_s))): %.hip_o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@



# Static lib
${KAAPIDIR}/kaapi_plugin_host_a.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_host.c ${KAAPIDIR}/kaapi_plugin.h  Makefile 
	$(CC) -c ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_host.c -o $@

${KAAPIDIR}/kaapi_plugin_cuda_a.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_cuda.c ${KAAPIDIR}/kaapi_plugin.h  Makefile 
	$(CC) -c ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_cuda.c -o $@

${KAAPIDIR}/kaapi_plugin_hip_a.o: ${FILE_LIB} ${KAAPIDIR}/kaapi_plugin_hip.c ${KAAPIDIR}/kaapi_plugin.h  Makefile 
	$(CC) -c ${CPPFLAGS} ${OPT} ${KAAPIDIR}/kaapi_plugin_hip.c -o $@

$(patsubst %.c,%_a.o,$(filter %.c,$(FILE_OTHER))): %_a.o:	 %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@ 

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_z))): %_a.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_c))): %_a.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_d))): %_a.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_s))): %_a.o: %.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_a.o,$(filter %.c,$(FILE_OTHER))): %.hip_a.o:	 %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@

$(patsubst %.c,%.hip_a.o,$(filter %.c, $(FILE_PRECISION_z))): %.hip_a.o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_a.o,$(filter %.c, $(FILE_PRECISION_c))): %.hip_a.o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_a.o,$(filter %.c, $(FILE_PRECISION_d))): %.hip_a.o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.hip_a.o,$(filter %.c, $(FILE_PRECISION_s))): %.hip_a.o: %.hip.c Makefile ${KAAPIDIR}/kaapi_impl.h ${KAAPIDIR}/kaapi.h ${KAAPIDIR}/kaapi_offload.h ${KAAPIDIR}/kaapi_memory.h ${KAAPIDIR}/kaapi_atomic.h ${KAAPIDIR}/kaapi_error.h ${KAAPIDIR}/kaapi_offload_stream.h kaapi_version.h ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -fPIC -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@



# Wrapper file for testing
$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_z))): %_wrap.o: %.c Makefile ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_c))): %_wrap.o: %.c Makefile ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_d))): %_wrap.o: %.c Makefile ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_s))): %_wrap.o: %.c Makefile  ${BLASDIR}/xkblas.h ${BLASDIR}/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@



#${XKBLAS_GEN_TASK} ${XKBLAS_GEN_BLAS} ${XKBLAS_GEN_TESTING} .generated

clean:
	rm -f libkaapi.a libkaapi.so libxkblas.a libxkblas.so libxkblas_blaswrapper.so ${KAAPIDIR}/*.o ${BLASDIR}/*.o ${BLASDIR}/*.hip_o ${BLASDIR}/*.hip_a.o ${BLASDIR}/*.hip.c ${BLASDIR}/common.h testing/*.o\
		libkaapi_plugin_host.so.1 libkaapi_plugin_cuda.so.1 libkaapi_plugin_hip.so.1\
		testing_z testing_c testing_d testing_s

