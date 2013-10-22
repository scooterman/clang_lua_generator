Clang Lua Generator
===================

This is a simple tool to  parse and generate Lua bindings using clang tooling infraestructure. It's supposed to receive as inputa a compilation database and a set of files to detect wich headers to process. As output you should receive a json file with all the necessary information to correctly generate lua source code (preferably using github.com/scooterman/pymunch tool).


# Usage:

./clang_lua_generator -o dump.json -p (PATH_TO_GENERATED_CMAKE_DB) (source files to try to parse)

the PATH_TO_GENERATED_CMAKE_DB can be generated using a cmake-aware project and passing -DCMAKE_EXPORT_COMPILE_COMMANDS=ON while configuring the project.
