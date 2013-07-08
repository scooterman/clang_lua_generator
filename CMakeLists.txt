project(clang-lua-generator)

cmake_minimum_required(VERSION 2.8)

include_directories(include)
include_directories(${CMAKE_BINARY_DIR})
include_directories(test)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(CMAKE_CXX_FLAGS "-std=c++11")

include_directories(${CLANG_INCLUDE})

exec_program(llvm-config ARGS --cxxflags OUTPUT_VARIABLE llvm_output_flags)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${llvm_output_flags}")

#exec_program(llvm-config ARGS --ldflags --libs  OUTPUT_VARIABLE llvm_lib_flags)
#set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${llvm_lib_flags}")

#exec_program(llvm-config ARGS  --libs OUTPUT_VARIABLE llvm_lib_flags)
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${llvm_lib_flags}")

#exec_program(llvm-config ARGS --libdir OUTPUT_VARIABLE llvm_libdir)
#link_directories(${llvm_libdir})

add_executable(clang-lua-generator src/cllua.cpp src/JsonValue.cpp)
target_link_libraries(clang-lua-generator LLVM-3.2 clangFrontend clangSerialization clangDriver
                   clangTooling clangParse clangSema clangAnalysis
                   clangRewriteFrontend clangRewriteCore clangEdit clangAST
                   clangLex clangBasic )

