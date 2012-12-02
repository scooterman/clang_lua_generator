#ifndef TEST1_HPP
#define TEST1_HPP

#include <iostream>

class Test1
{
public:
    Test1() {}
    
    Test1(int cocada) { std::cout << "cocada: " << cocada << std::endl; }
    
    void method() { std::cout << "calling method with no parameter" << std::endl; } 
    void method(int a, float b, const std::string& c) { std::cout << "calling method with 3 parameters" << std::endl; }
    void method(const std::string& value) { std::cout << "calling method overload with string contents:" << value << std::endl; }
    void method(const float value) { std::cout << "calling method overload with float contents:" << value << std::endl; }
};

#endif // TEST1_HPP
