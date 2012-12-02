project(clang-lua-generator)

cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "-std=c++11")

exec_program(llvm-config ARGS --cxxflags OUTPUT_VARIABLE llvm_output_flags)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${llvm_output_flags} -O0 -fno-rtti")

exec_program(llvm-config ARGS --ldflags --libs core mc OUTPUT_VARIABLE llvm_lib_flags)
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${llvm_lib_flags}")

exec_program(llvm-config ARGS --libdir OUTPUT_VARIABLE llvm_libdir)
link_directories(${llvm_libdir})

add_executable(clang-lua-generator src/clang_lua.cpp)
#add_executable(lua_test test/test.cpp)

target_link_libraries(clang-lua-generator pthread dl clang clangTooling)
#target_link_libraries(lua_test lua)

include_directories(include)
