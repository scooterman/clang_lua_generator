/*
    Copyright 2011 Victor Vicente de Carvalho victor (dot) v (dot) carvalho @ gmail (dot) com

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <functional>

#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <unordered_map>
#include <sstream>

using namespace clang;
using namespace std;
using namespace clang::tooling;

std::string toVarName(std::string className) {
    std::string res;

    for (auto i = 0u; i < className.size(); ++i) {
        if (std::isupper(className[i])) {
            if (i > 0) res += "_";
            res += std::tolower(className[i]);
        } else {
            res += className[i];
        }
    }

    return res;
}

std::string normalizeClass(const std::string& className) {
    std::string res;

for (auto c : className) {
        if (c == ':' || c == '<' || c == '>') {
            res += '_';
        } else {
            res += c;
        }
    }

    return res;
}

std::string getCanonicalTypeFromQualifiedType(const QualType& type) {
    LangOptions lo;
    PrintingPolicy pp(lo);
    pp.Bool = true;
    pp.SuppressTagKeyword = true;
    
    return type.getNonReferenceType().getCanonicalType().getUnqualifiedType().getAsString(pp);
}

class LuaClassContainer {
public:
    ///this map will store the methods that can be called from lua.
    std::unordered_map< std::string , std::string > visibleToLuaMethodMapping;
    ///this map will store the methods that are needed internally, like overloads
    std::unordered_map< std::string , std::string > internalMethodMapping;
    
    std::set < LuaClassContainer* > dependencies;
    std::string className;
    unsigned int classID;
    
    bool processed = false;
    bool pending = true;
    bool hasConstructors = false;
    
    virtual void dumpBindings() {
        std::cout << "processando: "<< className << " pendente? " << pending << std::endl;
        processed = true;

        for (auto dependency: dependencies) {
            if (!dependency->processed) {
                dependency->dumpBindings();
            }
        }

        std::stringstream classDef;
        std::stringstream methodTable;
        std::string normalizedClass = normalizeClass(className);

        classDef << "class Lua" << normalizedClass << " {" << std::endl;
        classDef << "public:" << std::endl;


        classDef << "static luaL_reg methods["<< visibleToLuaMethodMapping.size() + 1 <<"];" << std::endl;

        for(auto& method: visibleToLuaMethodMapping) {
            classDef << method.second << std::endl;
        }
        
         for(auto& method: internalMethodMapping) {
            classDef << method.second << std::endl;
        }

        classDef << " static "<< className << "* get(lua_State *L, int narg) {\n"
                 "    luaL_checktype(L, narg, LUA_TUSERDATA);\n"
                 "    void *ud = luaL_checkudata(L, narg, \"" << normalizedClass << "\");\n"
                 "    if(!ud) luaL_typerror(L, narg, \"" << normalizedClass << "\");\n"
                 "    return *("<< className <<"**)ud;\n"
                 "}\n";
     
        classDef << "static int gc_" << normalizedClass << "(lua_State* L) {\n"
                 "       " << className << "* instance = (" << className << "*) (*(void **)(lua_touserdata(L, 1)));\n"
                 "       delete instance;\n"
                 "       return 0;\n"
                 "}\n";

        classDef << "static void registerCppObject(lua_State* L) {\n"
                 "       lua_newtable(L);\n"
                 "       int methodtable = lua_gettop(L);\n"
                 "       luaL_newmetatable(L, \"" << normalizedClass << "\");\n"
                 "       int metatable   = lua_gettop(L);\n"
                 "\n"
                 "       lua_pushliteral(L, \"__metatable\");\n"
                 "       lua_pushvalue(L, methodtable);\n"
                 "       lua_settable(L, metatable);\n"  // hide metatable from Lua getmetatable()
                 "\n"
                 "       lua_pushliteral(L, \"__index\");\n"
                 "       lua_pushvalue(L, methodtable);\n"
                 "       lua_settable(L, metatable);\n"
                 
                 "\n"
                 "       lua_pushliteral(L, \"__native_class_specifier\");\n"
                 "       lua_pushinteger(L, " << classID << ");\n"
                 "       lua_settable(L, metatable);\n"
                 
                 "\n"
                 "       lua_pushliteral(L, \"__gc\");\n"
                 "       lua_pushcfunction(L, gc_" << normalizedClass << ");\n"
                 "       lua_settable(L, metatable);\n"

                 
                 "       lua_pop(L, 1);\n"  // drop metatable

                 "       luaL_openlib(L, 0, methods, 0);\n"  // fill methodtable
                 "       lua_pop(L, 1);\n";

        if (hasConstructors) {
            classDef << "       lua_register(L, \"" << normalizedClass << "\", create );\n";
        }

        classDef << "}\n";

        classDef << "};" << std::endl;

        for(auto method: visibleToLuaMethodMapping) {
            methodTable << "{ \"" << method.first << "\", Lua" << normalizedClass << "::" << method.first << "}," << std::endl;
        }

        classDef << "luaL_reg Lua" << normalizedClass << "::methods[" << visibleToLuaMethodMapping.size() + 1 << "] = { " << methodTable.str() << "{0,0} };" << std::endl;

        std::cerr << classDef.str() << std::endl;
    }
};

class LuaSharedPtrContainer : public LuaClassContainer {
public:
    LuaSharedPtrContainer(const std::string& _className) {
        className = _className;
    }

    virtual void dumpBindings() {
        processed = true;
        std::cout << "shared ptrrrr" << std::endl;
    }
};

struct LuaConversor {
    std::unordered_map< std::string, LuaClassContainer*  > classMapping;

    void dumpBindings() {
        std::cerr << "#include <lua.hpp>" << std::endl;
        std::cerr << "#include <cassert>" << std::endl;
//         std::cerr << "#include <gdx-cpp/graphics/Texture.hpp>" << std::endl << std::endl;

        for(auto clazz : classMapping) {
            std::cout << clazz.first << " " << clazz.second->className << std::endl;
            clazz.second->dumpBindings();
        }
    }
};

static LuaConversor conversor;
std::string makeFunctionVariable(QualType& paramType, const std::string& declName, int index, LuaClassContainer* owner = nullptr) {
    std::stringstream result;

    if (paramType->isIntegerType()) {
        result << "lua_Integer " << declName << " = luaL_checkinteger(L," << index << ");" << std::endl;
    } else if (paramType->isFloatingType()) {
        result << "lua_Number " << declName << " = luaL_checknumber(L," << index << ");" << std::endl;
    } else {
        std::string paramClassName = getCanonicalTypeFromQualifiedType(paramType);

        if (paramClassName == "std::basic_string<char>" || paramClassName == "char *" || paramClassName == "const char *") {
            result << "const char* " << declName << " = luaL_checkstring(L," << index << ");" << std::endl;
        } else {
            if (paramClassName.find("std::shared_ptr") != std::string::npos) {

            }

            if (!conversor.classMapping.count(paramClassName)) {
                conversor.classMapping[paramClassName] = new LuaClassContainer;
                conversor.classMapping[paramClassName]->className = paramClassName;
            }

            if (owner && paramClassName != owner->className) {
                owner->dependencies.insert(conversor.classMapping[paramClassName]);
            }

            result << "    " << paramClassName << "* "<< declName <<" = Lua"<< normalizeClass(paramClassName) << "::" << "get(L," << index << ");" << std::endl;
        }
    }

    return result.str();
}

typedef std::function< std::string (CXXMethodDecl&, LuaClassContainer&, int) > builderCallback;

std::string buildDefaultConstructor(LuaClassContainer& clazz) {
    std::stringstream funcBuffer;
    
    funcBuffer << "static int create";
    funcBuffer << "(lua_State* L) {" << std::endl;
    funcBuffer << "    " << clazz.className <<  "* instance = new " << clazz.className << "();" << std::endl;
    funcBuffer << "    (*(void **)(lua_newuserdata(L, sizeof(void *))) = (instance));" << std::endl;
    funcBuffer << "    luaL_getmetatable(L, \"" << clazz.className  << "\");" << std::endl;
    funcBuffer << "    lua_setmetatable(L, -2);" << std::endl;
    funcBuffer << "    return 1;" << std::endl;
    funcBuffer << "}" << std::endl;
    
    return funcBuffer.str();
}

std::string buildConstructor(CXXMethodDecl& method, LuaClassContainer& clazz, int methodIndex = -1) {
    std::stringstream funcBuffer;
    
    funcBuffer << "static int create";
    if (methodIndex >= 0) funcBuffer << "_lua_overload__" << methodIndex;    
    funcBuffer << "(lua_State* L) {" << std::endl;
    
    unsigned int paramCount = 0;
    std::string functionParams;
    for (auto param = method.param_begin(); param != method.param_end(); ++param) {
        QualType paramType = (*param)->getType();

        const auto& declName = (*param)->getDeclName().getAsString();
        
        //the first parameter is on index 1 on stack, on constructors
        funcBuffer << "    " << makeFunctionVariable(paramType, declName, paramCount + 1) << std::endl;

        functionParams +=  (paramType->isPointerType() ? "*" : "") + declName;

        if (++paramCount < method.param_size()) {
            functionParams += ',';
        }
    }
        
    funcBuffer << "    " << clazz.className <<  "* instance = new " << clazz.className << "(" << functionParams << ");" << std::endl;
    funcBuffer << "    (*(void **)(lua_newuserdata(L, sizeof(void *))) = (instance));" << std::endl;
    funcBuffer << "    luaL_getmetatable(L, \"" << clazz.className  << "\");" << std::endl;
    funcBuffer << "    lua_setmetatable(L, -2);" << std::endl;
    funcBuffer << "    return 1;" << std::endl;
    funcBuffer << "}" << std::endl;
        
    return funcBuffer.str();
}

std::string buildFunction(CXXMethodDecl& method, LuaClassContainer& clazz, int methodIndex = -1) {
    std::stringstream funcBuffer;
        
    funcBuffer << "static int " << method.getNameAsString();
    if (methodIndex >= 0) funcBuffer << "_lua_overload__" << methodIndex;    
    funcBuffer << "(lua_State* L) {" << std::endl;
    
    funcBuffer << "    " << clazz.className << "* instance = get(L, 1);" << std::endl;

    unsigned int paramCount = 0;
    std::string functionParams;
    for (auto param = method.param_begin(); param != method.param_end(); ++param) {
        QualType paramType = (*param)->getType();

        const auto& declName = (*param)->getDeclName().getAsString();
        
        //the first parameter is on index 2 on stack
        funcBuffer << "    " << makeFunctionVariable(paramType, declName, paramCount + 2) << std::endl;

        functionParams +=  (paramType->isPointerType() ? "*" : "") + declName;

        if (++paramCount < method.param_size()) {
            functionParams += ',';
        }
    }

    if (!method.getResultType()->isVoidType()) {
        std::string paramClassName = getCanonicalTypeFromQualifiedType(method.getResultType());
        funcBuffer << "    " << paramClassName << " returnType =";
    }

    funcBuffer << "     instance->" << method.getNameAsString() << "(" << functionParams << ");" << std::endl;
    funcBuffer << "     return " <<  (method.getResultType()->isVoidType() ? "0;" : "1;") << std::endl;
    funcBuffer << "}" << std::endl;
    
    return funcBuffer.str();
}



std::string makeOverloadedFunction(LuaClassContainer& clazz, const std::string& functionName, 
                            std::vector<CXXMethodDecl>& clashingMethods, const builderCallback callback, int stackItems) {

    int methodResolutionCount = 0;
    
    // the ones that fall here can be resolved with a switch/case with the parameter count
    // this means that there is only one method/function argument count
    std::vector< CXXMethodDecl > overloadWithDifferentParameterCount;

    // these have to do some checking with types, but do not need to rely on typeinfo to do that
    std::vector< CXXMethodDecl > overloadWithTypesThatCanBeFigurable;

    // these have to use type information resolving, which is (possibly) the slower
    std::vector< CXXMethodDecl > overloadWithTypeInfo;

    // Putting everything under its places
    for (const auto& methodDecl: clashingMethods) {
        // type 1
        bool sameQty = false;
        for (const auto& other: clashingMethods) {            
            if (&other == &methodDecl) {
                continue;
            }

            if (other.getNumParams() == methodDecl.getNumParams()) {
                sameQty = true;
                break;
            }
        }

        if (!sameQty) {
            overloadWithDifferentParameterCount.push_back(methodDecl);
            continue;
        }

        //type 2
        bool sameParamType = false;
        for (const auto& other: clashingMethods) {
            if (&other == &methodDecl) {
                continue;
            }
            
            for (auto p1 = methodDecl.param_begin(), p2 = other.param_begin();
                    p1 != methodDecl.param_end() && p2 != other.param_end(); 
                    ++p1,++p2) {
                if (p1 == p2 ) {
                    sameParamType = true;
                    break;
                }
            }
            
            if (sameParamType) {
                break;
            }
        }
        
        if (!sameParamType) {
            overloadWithTypesThatCanBeFigurable.push_back(methodDecl);
            continue;
        }
        
        
        //TODO: type 3
    }

    std::stringstream buffer;

    buffer << "static int " << functionName << "(lua_State* L) {" << std::endl;
    
    if (!overloadWithDifferentParameterCount.empty()) {
        buffer << "   switch(lua_gettop(L)) {" << std::endl;

        for (auto method: overloadWithDifferentParameterCount) {
            buffer << "    case " << (method.getNumParams() + stackItems) << ":" <<std::endl;
            buffer << "         return " << functionName << "_lua_overload__" << methodResolutionCount << "(L);" <<std::endl;

            
            std::stringstream currentfunc;
            currentfunc << functionName << "_lua_overload__" << methodResolutionCount;
            //now build the overloaded method with a new name resolution
             conversor.classMapping[clazz.className]->internalMethodMapping[currentfunc.str()] = callback(method, clazz, methodResolutionCount++);
        }        
        buffer << "    }" << std::endl;
    }

    if (!overloadWithTypesThatCanBeFigurable.empty()) {
        for (auto& method: overloadWithTypesThatCanBeFigurable) {
            buffer << "if (";

            int paramStart = stackItems + 1;
            for (auto param = method.param_begin(); param != method.param_end(); ++param) {
                QualType paramType = (*param)->getType();

                if (paramType->isIntegerType() || paramType->isFloatingType()) {;
                    buffer << "lua_type(L," << paramStart++ << ") == LUA_TNUMBER && ";
                } else if (paramType->isBooleanType()) {
                    buffer << "lua_type(L," << paramStart++ << ") == LUA_TBOOLEAN && ";
                } else {
                    std::string qtype = getCanonicalTypeFromQualifiedType(paramType);

                    if (qtype == "std::basic_string<char>" || qtype == "const char*") {
                        buffer << " lua_type(L," << paramStart++ << ") == LUA_TSTRING && ";
                    } else {
                        buffer << " lua_type(L," << paramStart++ << ") == LUA_TUSERDATA && ";
                    }
                }
            }

            buffer << "true) { return " << functionName << "_lua_overload__" << methodResolutionCount << "(L); }" << std::endl;

            std::stringstream currentfunc;
            currentfunc << functionName << "_lua_overload__" << methodResolutionCount;
            
            conversor.classMapping[clazz.className]->internalMethodMapping[currentfunc.str()] = callback(method, clazz, methodResolutionCount++);
        }
    }
    
    buffer << "    assert(false);" << std::endl;
    buffer << "}" << std::endl;
    //TODO: the latest and pitaest one :(    
    return buffer.str();
}

std::string makeOverloadedForFunction(LuaClassContainer& clazz, const std::string& functionName, 
                            std::vector<CXXMethodDecl>& clashingMethods, const builderCallback& callback) {
    return makeOverloadedFunction(clazz, functionName, clashingMethods, callback, 1);
}

std::string makeOverloadedForConstructor(LuaClassContainer& clazz, const std::string& functionName, 
                            std::vector<CXXMethodDecl>& clashingMethods, const builderCallback& callback) {
    return makeOverloadedFunction(clazz, functionName, clashingMethods, callback, 0);
}

class LuaBuilderASTVisitor: public RecursiveASTVisitor<LuaBuilderASTVisitor> {
public:
    LuaBuilderASTVisitor(SourceManager& manager): sourceManager(manager) {
    }

    virtual bool VisitCXXRecordDecl(CXXRecordDecl* record) {
        if (sourceManager.getFileID ( record->getLocation() ) != sourceManager.getMainFileID() ) {
            return true;
        }

        if(!record->isClass()) {
            return true;
        }

        std::string className = record->getNameAsString();
    
        if (conversor.classMapping.count(className) == 0) {
            conversor.classMapping[className] = new LuaClassContainer;
            conversor.classMapping[className]->className = className;
        }
        
        LuaClassContainer* clazz = conversor.classMapping[className];
        clazz->pending = false;
        
        std::vector < CXXMethodDecl > constructors;
        //parsing constructors        
        for(auto it = record->ctor_begin(); it != record->ctor_end(); ++it) {
            if (it->getAccess() != AS_public && it->getAccess() != AS_none) {
                continue;
            }
            
            constructors.push_back(**it);
        }
        
        //if the default constructor is undeclared, manually create one with no parameters
        if (constructors.empty()) {
            if (!record->hasDeclaredDefaultConstructor()) {
                std::cout << "adding default constructor" << std::endl;
                clazz->visibleToLuaMethodMapping["create"] = buildDefaultConstructor(*clazz);
                clazz->hasConstructors = true;
            }
        } else {            
            clazz->visibleToLuaMethodMapping["create"] = makeOverloadedForConstructor(*clazz, "create", constructors, buildConstructor );
            clazz->hasConstructors = true;
        }

        std::unordered_map< std::string, std::vector <CXXMethodDecl > > methodsWithOverload;
        
        //parsing methods and composing overloads based on class name      
        for (auto method = record->method_begin(); method != record->method_end(); ++method) {
            if (method->isOverloadedOperator() || method->getAccess() != AS_public 
                || isa<CXXConstructorDecl>(*method) || isa<CXXDestructorDecl>(*method)) {
                continue;
            }
            
            methodsWithOverload[(*method)->getNameAsString()].push_back(**method);
        }
        
        for (auto& tuple: methodsWithOverload) {
            if (tuple.second.size() > 1) {
                std::cout << "method: [" << tuple.first << "] has " << tuple.second.size() << " overloads. Processing..." << std::endl;
                clazz->visibleToLuaMethodMapping[tuple.first] = makeOverloadedForFunction(*clazz, tuple.first, tuple.second, buildFunction );
            } else {
                std::cout << "method: [" << tuple.first << "] has no overloads. Processing..." << std::endl;
                clazz->visibleToLuaMethodMapping[tuple.first] = buildFunction(tuple.second[0], *clazz);
            }
        }
        
        return true;
    }

private:
    SourceManager& sourceManager;
};

class LuaBinderConsumer : public ASTConsumer {
public:
    LuaBinderConsumer (SourceManager& manager) : Visitor(manager) {
    }

    virtual void HandleTranslationUnit ( clang::ASTContext &Context ) {
        Visitor.TraverseDecl ( Context.getTranslationUnitDecl() );
    }

    virtual ~LuaBinderConsumer() {
    }

private:
    LuaBuilderASTVisitor Visitor;
};

class BuildLuaBindingsAction : public ASTFrontendAction {
public:

    BuildLuaBindingsAction() { }
    virtual clang::ASTConsumer *CreateASTConsumer (
        clang::CompilerInstance &Compiler, llvm::StringRef InFile ) {
        //Compiler.getDiagnostics().setSuppressAllDiagnostics(true);
        tool = new LuaBinderConsumer(Compiler.getSourceManager());
        return tool;
    }

    LuaBinderConsumer* tool;
};

int main ( int argc, const char** argv ) {
    CommonOptionsParser parser( argc, argv );
    ClangTool tool(parser.GetCompilations(), parser.GetSourcePathList());

    int result = tool.run ( newFrontendActionFactory<BuildLuaBindingsAction>() );

    if (!result) {
        conversor.dumpBindings();
    }

    return result;
}



