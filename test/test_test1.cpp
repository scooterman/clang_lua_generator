#include "Test1.hpp"
#include "LuaGenTest1.hpp"
#include <iostream>

inline void run_check_error(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        const char* errorMsg = lua_tostring(L, -1);
        std::cerr << "error running code: " << code << std::endl << "error: " << errorMsg << std::endl;
        assert(false);
    }
}

int main() {
    auto L = lua_open();
    
    luaL_openlibs(L);

    LuaTest1::registerCppObject(L);
    
    run_check_error(L, "local x = Test1(); x:method();");
    run_check_error(L, "local x = Test1(); x:method(1, 2.0, 'test');");
    run_check_error(L, "local x = Test1(); x:method('testing');");
    run_check_error(L, "local x = Test1(42);");
    
    return 0;
}
