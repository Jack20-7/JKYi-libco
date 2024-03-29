cmake_minimum_required(VERSION 2.8)
project(JKYi-libco)

set(JKYI-LIBCO 1.0)

set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} -g -fno-strict-aliasing -O2 -Wall -export-dynamic -Wall -pipe -D_GNU_SOURCE -D_REENTRANT -fPIC -Wno-deprecated -m64)

enable_language(C ASM)

include_directories(/usr/local/openssl/include/)
include_directories(/home/admin/JKYi-libco/)
link_libraries(/usr/local/openssl/lib64)

set(SOURCE_FILES
      co_epoll.cc
      co_hook_sys_call.cc
      co_routine.cc
      coctx.cc
      coctx_swap.S
      co_comm.cc
   )
#生成静态库
add_library(colib_static STATIC ${SOURCE_FILES})

#生成动态库
add_library(colib_shared SHARED ${SOURCE_FILES})

#set lib output name
#为了能够输出拥有相同名称的库
#下面会生成colib.a  colib.so
set_target_properties(colib_static PROPERTIES OUTPUT_NAME colib)
set_target_properties(colib_shared PROPERTIES OUTPUT_NAME colib)

#避免在构建库时出现冲突
set_target_properties(colib_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(colib_shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)

#set lib version
set_target_properties(colib_shared PROPERTIES VERSION ${JKYI-LIBCO} SOVERSION ${JKYI-LIBCO})

macro(add_example_target EXAMPLE_TARGET)
     add_executable("example_${EXAMPLE_TARGET}" "tests/example_${EXAMPLE_TARGET}.cc")
     target_link_libraries("example_${EXAMPLE_TARGET}" colib_static pthread dl)
endmacro(add_example_target)

add_example_target(thread)
add_example_target(cond)
add_example_target(copystack)
add_example_target(poll)
add_example_target(specific)
add_example_target(setenv)
add_example_target(echosvr)
add_example_target(echocli)

add_executable(example_websocket tests/example_websocket.cc)
target_link_libraries(example_websocket colib_static pthread dl ssl crypto)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

