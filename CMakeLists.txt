project(clang-lua-generator)

cmake_minimum_required(VERSION 2.8)

include_directories(include)
include_directories(${CMAKE_BINARY_DIR})
include_directories(test)

set(CMAKE_CXX_FLAGS "-std=c++11")

exec_program(llvm-config ARGS --cxxflags OUTPUT_VARIABLE llvm_output_flags)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${llvm_output_flags} -O0 -fno-rtti")

exec_program(llvm-config ARGS --ldflags --libs core mc OUTPUT_VARIABLE llvm_lib_flags)
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${llvm_lib_flags}")

exec_program(llvm-config ARGS --libdir OUTPUT_VARIABLE llvm_libdir)
link_directories(${llvm_libdir})

add_executable(clang-lua-generator src/cllua.cpp src/JsonValue.cpp)
target_link_libraries(clang-lua-generator pthread dl clang clangTooling)

if (TEST) 
    add_definitions(-DLUA_COMPAT_MODULE)

    set(CMAKE_CXX_FLAGS -std=c++11)
    add_executable(test1 test/test_test1.cpp generated.cpp)

    set(CMAKE_CXX_FLAGS -std=c++11)
    add_executable(test2 test/test_test2.cpp generated.cpp)

    target_link_libraries(test2 /usr/lib64/liblua-5.1.so)

    target_link_libraries(test1 /usr/lib64/liblua-5.1.so)
endif()