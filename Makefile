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

ifeq ($(KAAPI_USE_DYNLOADER),0)
  $(info Configure with plugin statically linked into libkaapi)
else
  $(info Configure with dynamic loaded plugin)
endif

ifdef HWLOC_HOME
#  $(info  "$$(HWLOC_HOME)=$(HWLOC_HOME)  is defined - use HWLOC")
  HWLOC_FLAGS=-I${HWLOC_HOME}/include -DKAAPI_USE_HWLOC=1
  HWLOC_LIBS=-Wl,-rpath=${HWLOC_HOME}/lib -L${HWLOC_HOME}/lib -lhwloc
else
#  $(info  "$$(HWLOC_HOME)  is not defined - do not use HWLOC")
  HWLOC_FLAGS=-DKAAPI_USE_HWLOC=0
endif

ifdef CUDA_HOME
  CUDA_FLAGS=-I${CUDA_HOME}/include -DKAAPI_USE_CUDA=1
  CUDA_LIBS=-Wl,-rpath=${CUDA_HOME}/lib64 -L${CUDA_HOME}/lib64 -lcublas -lcuda -lcudart
  CUDA_KAAPI_PLUGIN=libkaapi_plugin_cuda.so.1
  CUDA_KAAPI_PLUGIN_C=./kaapi_plugin_cuda.c
  $(info CUDA defined and used)
else
  CUDA_FLAGS=-DKAAPI_USE_CUDA=0
  $(info $$(CUDA_HOME) is not defiend - do not use CUDA)
endif


$(info Using $(BLAS_LIB_SO) with flags: $(BLAS_CPPFLAGS))

#
# Common C flags and libs
#
CPPFLAGS=${CUDA_FLAGS} ${HWLOC_FLAGS}
LDFLAGS=${CUDA_LIBS} ${HWLOC_LIBS}


#
# UKAAPI sub library
#
UKAAPI_LIBNAME=libkaapi.so
UKAAPI_LIBNAME_A=libkaapi.a
UKAAPI_SRC=./kaapi_format.c ./kaapi_impl.c  ./kaapi_task.c ./kaapi_rt.c  ./kaapi_hashmap.c ./kaapi_barrier.c ./kaapi_memory.c ./kaapi_offload_stream.c ./kaapi_offload.c ./kaapi_offload_device.c  ./kaapi_ld.c ./kaapi_dbg.c
UKAAPI_FILE_LIB=${UKAAPI_SRC} ./kaapi_impl.h ./kaapi.h kaapi_offload_stream.h kaapi_offload.h kaapi_offload_dbg.h kaapi_plugin.h ./kaapi_atomic.h ./kaapi_error.h ./kaapi_format.h ./kaapi_hashmap.h ./kaapi_memory.h  kaapi_version.h

UKAAPI_SRC_PLUGIN=${CUDA_KAAPI_PLUGIN_C} ./kaapi_plugin_host.c
UKAAPI_FILE_PLUGIN=${UKAAPI_SRC_PLUGIN} kaapi_plugin.h

ifeq ($(KAAPI_USE_DYNLOADER),0)
  # merge FILE_PLUGIN in normal library
  UKAAPI_FILE_LIB+=$(UKAAPI_FILE_PLUGIN)
  UKAAPI_TARGET_PLUGIN=
  UKAAPI_LDFLAGS=${LDFLAGS} -fopenmp
else
  UKAAPI_LDFLAGS="-ldl"
  UKAAPI_TARGET_PLUGIN=libkaapi_plugin_host.so.1 ${CUDA_KAAPI_PLUGIN}
endif


#
# LIBXKBLAS
#
XKBLAS_BLAS_PRECISION_s=\
  blas/sgemm.c \
  blas/sgemmt.c \
  blas/strsm.c \
  blas/strmm.c \
  blas/ssymm.c \
  blas/ssyrk.c \
  blas/ssyr2k.c \
  blas/xkblas_s.h\
  blas/xkblas_f77_s.h

XKBLAS_BLAS_PRECISION_d=\
  blas/dgemm.c\
  blas/dgemmt.c\
  blas/dtrsm.c\
  blas/dtrmm.c\
  blas/dsymm.c\
  blas/dsyrk.c\
  blas/dsyr2k.c\
  blas/xkblas_d.h\
  blas/xkblas_f77_d.h

XKBLAS_BLAS_PRECISION_c=\
  blas/cgemm.c\
  blas/cgemmt.c\
  blas/ctrsm.c\
  blas/ctrmm.c\
  blas/csymm.c\
  blas/csyrk.c\
  blas/csyr2k.c\
  blas/chemm.c \
  blas/cherk.c \
  blas/cher2k.c \
  blas/xkblas_c.h\
  blas/xkblas_f77_c.h

XKBLAS_BLAS_PRECISION_z=\
  blas/zgemm.c\
  blas/zgemmt.c\
  blas/ztrsm.c\
  blas/ztrmm.c\
  blas/zsymm.c\
  blas/zsyrk.c\
  blas/zsyr2k.c\
  blas/zhemm.c \
  blas/zherk.c \
  blas/zher2k.c \
  blas/xkblas_z.h\
  blas/xkblas_f77_z.h

XKBLAS_GEN_BLAS=blas/internal_register.h\
  ${XKBLAS_BLAS_PRECISION_s}\
  ${XKBLAS_BLAS_PRECISION_d}\
  ${XKBLAS_BLAS_PRECISION_c}

XKBLAS_BLAS=\
  ${XKBLAS_BLAS_PRECISION_z}\
  ${XKBLAS_GEN_BLAS}

XKBLAS_TASK_PRECISION_s=\
  blas/task_sgemm.c\
  blas/task_ssymm.c\
  blas/task_sgemmt.c\
  blas/task_strsm.c\
  blas/task_strmm.c\
  blas/task_ssyrk.c\
  blas/task_ssyr2k.c\
  blas/task_s.h \
  blas/task_s_internal.h

XKBLAS_TASK_PRECISION_d=\
  blas/task_dgemm.c\
  blas/task_dsymm.c\
  blas/task_dgemmt.c\
  blas/task_dtrsm.c\
  blas/task_dtrmm.c\
  blas/task_dsyrk.c\
  blas/task_dsyr2k.c\
  blas/task_d.h \
  blas/task_d_internal.h

XKBLAS_TASK_PRECISION_c=\
  blas/task_cgemm.c\
  blas/task_csymm.c\
  blas/task_cgemmt.c\
  blas/task_ctrsm.c\
  blas/task_ctrmm.c\
  blas/task_csyrk.c\
  blas/task_csyr2k.c\
  blas/task_chemm.c\
  blas/task_cherk.c\
  blas/task_cher2k.c\
  blas/task_c.h\
  blas/task_c_internal.h

XKBLAS_TASK_PRECISION_z=\
  blas/task_zgemm.c\
  blas/task_zsymm.c\
  blas/task_zgemmt.c\
  blas/task_ztrsm.c\
  blas/task_ztrmm.c\
  blas/task_zsyrk.c\
  blas/task_zsyr2k.c\
  blas/task_zhemm.c\
  blas/task_zherk.c\
  blas/task_zher2k.c\
  blas/task_z.h\
  blas/task_z_internal.h

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
XKBLAS_WRAPPER_SRC=blas/libxkblas_wrapper.c 
XKBLAS_WRAPPER_PRECISION_z=blas/libxkblas_wrapper_z.c 
XKBLAS_WRAPPER_PRECISION_c=blas/libxkblas_wrapper_c.c 
XKBLAS_WRAPPER_PRECISION_d=blas/libxkblas_wrapper_d.c 
XKBLAS_WRAPPER_PRECISION_s=blas/libxkblas_wrapper_s.c
XKBLAS_WRAPPER_PRECISION=${XKBLAS_WRAPPER_PRECISION_z} ${XKBLAS_WRAPPER_PRECISION_c} ${XKBLAS_WRAPPER_PRECISION_d} ${XKBLAS_WRAPPER_PRECISION_s}
XKBLAS_WRAPPER_GENFILES=blas/libxkblas_wrapper_c.c blas/libxkblas_wrapper_d.c blas/libxkblas_wrapper_s.c 
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
  blas/xkblas.h\
  blas/common.h\
  blas/xkblas.c\
  blas/dbg_blas.c\

XKBLAS_FILES=\
  ${XKBLAS_OTHER_FILES}\
  ${XKBLAS_BLAS}\
  ${XKBLAS_TASK}
XKBLAS_SRC=$(filter %.c, ${XKBLAS_FILES})
XKBLAS_CPPFLAGS=-I. -Iblas -DXKBLAS_BLASLIB='"${BLAS_LIB_SO}"' ${BLAS_CPPFLAGS} ${CPPFLAGS} 
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

FILE_OTHER=blas/xkblas.h blas/common.h\
  blas/xkblas.c blas/dbg_blas.c\
  blas/libxkblas_wrapper.c\
  blas/internal_register.h\
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
            ${XKBLAS_BLAS_PRECISION_z}
	touch .generated
	(cd blas ; ../gen_precision.sh)

.generated_testing: .generated gen_precision.sh \
            ${XKBLAS_TESTING_PRECISION_z}
	touch .generated_testing
	(cd testing ; ../gen_precision.sh)

${XKBLAS_ALL_GENFILES}: .generated .generated_testing

blas/libxkblas_wrapper_c.c: gen_precision_one.sh blas/libxkblas_wrapper_z.c
	(./gen_precision_one.sh c blas/libxkblas_wrapper_z.c)
blas/libxkblas_wrapper_d.c: gen_precision_one.sh blas/libxkblas_wrapper_z.c
	(./gen_precision_one.sh d blas/libxkblas_wrapper_z.c)
blas/libxkblas_wrapper_s.c: gen_precision_one.sh blas/libxkblas_wrapper_z.c
	(./gen_precision_one.sh s blas/libxkblas_wrapper_z.c)

ifneq ('$(GIT_HASH)','')
gitlastcommit:
	@echo "#define GIT_HASH ${GIT_HASH}" > .kaapi_version.h
	@if cmp --quiet .kaapi_version.h kaapi_version.h ; then \
		echo "Git version '${GIT_HASH}' not changed" ;\
	$(RM) .kaapi_version.h ;\
	else \
		echo "New git version, updating kaapi_version.h" ;\
		mv .kaapi_version.h kaapi_version.h ; \
	fi
else
gitlastcommit:
endif
kaapi_version.h:  gitlastcommit

#DISTTAG=`git describe --tags  --abbrev=0`-${GIT_HASH}
DISTTAG=${GIT_HASH}
dist:  .generated .generated_testing
	mkdir -p xkblas/blas xkblas/testing
	cp ${UKAAPI_FILE_LIB} ${UKAAPI_FILE_PLUGIN} AUTHORS COPYING LICENCE README.md make.inc Makefile xkblas/
	cp blas/libxkblas_wrapper.h blas/flops.h blas/xkblas_f77.h blas/task_format.h blas/xkblas_z.h blas/xkblas_f77_z.h blas/task_z.h blas/task_z_internal.h ${XKBLAS_FILES} ${XKBLAS_WRAPPER_SRC}${XKBLAS_WRAPPER_PRECISION} xkblas/blas
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
		cp kaapi.h kaapi_error.h kaapi_atomic.h blas/xkblas.h blas/xkblas_?.h ${PREFIX}/include; \
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

libkaapi_plugin_host.so.1: kaapi_plugin_host.o libkaapi.so
	$(CC) -shared -o libkaapi_plugin_host.so.1 kaapi_plugin_host.o ${UKAAPI_LDFLAGS}

libkaapi_plugin_cuda.so.1: kaapi_plugin_cuda.o libkaapi.so
	$(CC) -shared -fopenmp -o libkaapi_plugin_cuda.so.1 kaapi_plugin_cuda.o ${UKAAPI_LDFLAGS} ${HWLOC_LIBS} ${CUDA_LIBS}

libxkblas.so: libkaapi.so .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=.o} 
	echo ${XKBLAS_SRC:.c=.o}
	$(CC) -shared -o libxkblas.so ${XKBLAS_SRC:.c=.o} ${XKBLAS_LDFLAGS} ${XKBLAS_CPPFLAGS} -L${KAAPI_HOME} -lkaapi -lm

libxkblas.a: libkaapi.a .generated ${XKBLAS_ALL_GENFILES} ${XKBLAS_SRC:.c=_a.o} 
	$(AR) crv libxkblas.a ${XKBLAS_SRC:.c=_a.o} ${UKAAPI_SRC:%.c=%_a.o} ${UKAAPI_SRC_PLUGIN:.c=_a.o} 
	$(RANLIB) libxkblas.a

blas/libxkblas_wrapper.c: blas/libxkblas_wrapper_z.c blas/libxkblas_wrapper_c.c blas/libxkblas_wrapper_d.c blas/libxkblas_wrapper_s.c
blas/libxkblas_wrapper.c: blas/libxkblas_wrapper.h
blas/libxkblas_wrapper_z.c: blas/libxkblas_wrapper.h
blas/libxkblas_wrapper_c.c: blas/libxkblas_wrapper.h
blas/libxkblas_wrapper_d.c: blas/libxkblas_wrapper.h
blas/libxkblas_wrapper_s.c: blas/libxkblas_wrapper.h

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
kaapi_plugin_host.o: ${FILE_LIB} kaapi_plugin_host.c kaapi_plugin.h  Makefile 
	$(CC) -c -fPIC ${CPPFLAGS} ${OPT} ./kaapi_plugin_host.c

kaapi_plugin_cuda.o: ${FILE_LIB} kaapi_plugin_cuda.c kaapi_plugin.h  Makefile 
	$(CC) -c -fPIC ${CPPFLAGS} -fopenmp ${OPT} ./kaapi_plugin_cuda.c

$(patsubst %.c,%.o,$(filter %.c,$(FILE_OTHER))): %.o:	 %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -fPIC ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_z))): %.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -fPIC -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_c))): %.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -fPIC -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_d))): %.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -fPIC -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%.o,$(filter %.c, $(FILE_PRECISION_s))): %.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -fPIC -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@


# Static lib
kaapi_plugin_host_a.o: ${FILE_LIB} kaapi_plugin_host.c kaapi_plugin.h  Makefile 
	$(CC) -c ${CPPFLAGS} ${OPT} ./kaapi_plugin_host.c -o $@

kaapi_plugin_cuda_a.o: ${FILE_LIB} kaapi_plugin_cuda.c kaapi_plugin.h  Makefile 
	$(CC) -c ${CPPFLAGS} -fopenmp ${OPT} ./kaapi_plugin_cuda.c -o $@

$(patsubst %.c,%_a.o,$(filter %.c,$(FILE_OTHER))): %_a.o:	 %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) ${XKBLAS_CPPFLAGS} ${OPT} -DXKBLAS_CFLAGS='"${XKBLAS_CPPFLAGS} ${OPT}"' -c $<  -o $@ 

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_z))): %_a.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_c))): %_a.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_d))): %_a.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_a.o,$(filter %.c, $(FILE_PRECISION_s))): %_a.o: %.c Makefile kaapi_impl.h kaapi.h kaapi_offload.h kaapi_memory.h kaapi_atomic.h kaapi_error.h kaapi_offload_stream.h kaapi_version.h blas/xkblas.h blas/common.h .generated
	$(CC) -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@


# Wrapper file for testing
$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_z))): %_wrap.o: %.c Makefile blas/xkblas.h blas/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_z -UPRECISION_s -UPRECISION_d -UPRECISION_c ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_c))): %_wrap.o: %.c Makefile blas/xkblas.h blas/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_c -UPRECISION_s -UPRECISION_d -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_d))): %_wrap.o: %.c Makefile blas/xkblas.h blas/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_d -UPRECISION_s -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@

$(patsubst %.c,%_wrap.o,$(filter %.c, $(FILE_PRECISION_s))): %_wrap.o: %.c Makefile  blas/xkblas.h blas/common.h .generated
	$(CC) -DTESTING_API_XKBLAS_WRAPPER -DPRECISION_s -UPRECISION_d -UPRECISION_c -UPRECISION_z ${XKBLAS_CPPFLAGS} ${OPT} -c $<  -o $@



#${XKBLAS_GEN_TASK} ${XKBLAS_GEN_BLAS} ${XKBLAS_GEN_TESTING} .generated

clean:
	rm -f libkaapi.a libkaapi.so libxkblas.a libxkblas.so libxkblas_blaswrapper.so *.o blas/*.o testing/*.o\
		libkaapi_plugin_host.so.1 libkaapi_plugin_cuda.so.1\
		testing_z testing_c testing_d testing_s


