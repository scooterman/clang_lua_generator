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
#include <clang/Frontend/FrontendPluginRegistry.h>

#include <unordered_map>
#include <sstream>
#include "JsonValue.hpp"

using namespace clang;
using namespace std;
using namespace clang::tooling;

std::string getCanonicalTypeFromQualifiedType(const QualType& type) {
    LangOptions lo;
    PrintingPolicy pp(lo);
    pp.Bool = true;
    pp.SuppressTagKeyword = true;
    
    QualType qt = type;
    
    if (type->isPointerType()) {    
        qt = type->getPointeeType();
    }
   
    return qt.getLocalUnqualifiedType().getNonReferenceType().getCanonicalType().getUnqualifiedType().getAsString(pp);
}

struct CxxType {  
  bool isPrimitive = false;  
  bool isTypedef = false;
  
  std::string ns;
  std::string type;
  std::string spelling;
};

struct MethodParameter {
  CxxType* type = nullptr;
  bool isPointer = false;
  bool isReference = false;
  bool isConst = false;
  
  std::string name;
};

struct MethodDefinition {
    enum class FuncType {
        method,
        constructor
    };
    
    std::vector<MethodParameter> parameters;    
    std::string name;
    bool isVirtual = false;
    
    MethodParameter retType;    
    FuncType functionType = FuncType::method;
};

class ClassDefinition {
public:
    std::vector< MethodDefinition > methods;
    
    std::set < std::string > dependencies;
    std::set < ClassDefinition* > bases;
     
    std::string name;
    std::string qualifiedName;
    unsigned int classID = 0;
    bool processed = false;
    
    ClassDefinition(const std::string& _name, const std::string& _qualifiedName) : name(_name), qualifiedName(_qualifiedName) {
    }
};

std::unordered_map< std::string, ClassDefinition*  > classMapping;
std::unordered_map< std::string, CxxType* > typeMapping;

CxxType* makeType(const QualType& paramType) {
    std::string typeName = getCanonicalTypeFromQualifiedType(paramType);
    
    if (typeMapping.count(typeName)) {
        return typeMapping[typeName];
    }
    
    CxxType* type = new CxxType;
    
    type->spelling = typeName;
    
    std::string typeStr = typeName;
        
    if (typeName.find("<") != std::string::npos) {
        type->type = typeName.substr(0, typeName.rfind(">") + 1);
    } else {
        type->type = type->spelling;
    }
    
    if (type->type.find("::") != std::string::npos) {
        type->ns = type->type.substr(0, type->type.rfind("::"));
        type->type = type->type.substr(type->ns.length() + 2, type->type.length() - (type->ns.length() + 2));
    }

    typeMapping[typeName] = type;
    return type;
}

MethodParameter makeParameter(const QualType& param) {
    CxxType* type = makeType(param);

    MethodParameter cxxParam;

    cxxParam.type = type;
    cxxParam.isPointer = param->isPointerType();
    cxxParam.isReference = param->isReferenceType();
    cxxParam.isConst = param.getQualifiers().hasConst();
    
    return cxxParam;
}

template <typename dc>
MethodDefinition createMethod(MethodDefinition::FuncType ft, const dc& decl , ClassDefinition& cdef) {
    MethodDefinition md;
    md.name = decl.getNameAsString();
    md.functionType = ft;
    md.isVirtual = decl.isVirtual();
    
    for (auto param = decl.param_begin(); param != decl.param_end(); ++param) {
        QualType paramType = (*param)->getType();

        MethodParameter cxxParam = makeParameter(paramType);
        cxxParam.name = (*param)->getDeclName().getAsString();       

        if (cxxParam.type->spelling != cdef.name
            && !paramType->isIncompleteType()
                && paramType->isClassType()
                || (paramType->isPointerType() &&  paramType->getPointeeType()->isClassType())) {
            cdef.dependencies.insert(cxxParam.type->spelling);
        }

        md.parameters.push_back(cxxParam);
    }
    
    if (ft != MethodDefinition::FuncType::constructor) {
        QualType retType = decl.getResultType();
        md.retType =  makeParameter(retType);

        if (md.retType.type->spelling != cdef.name
                && !retType->isIncompleteType()
                && retType->isClassType()
                || (retType->isPointerType()
                    && retType->getPointeeType()->isClassType())) {
            cdef.dependencies.insert(md.retType.type->spelling);
        }
    }
    
    return md;
}

class LuaBuilderASTVisitor: public RecursiveASTVisitor<LuaBuilderASTVisitor> {
public:
    LuaBuilderASTVisitor(SourceManager& manager): sourceManager(manager) {
    }

    virtual bool VisitCXXRecordDecl(CXXRecordDecl* record) {
        if (sourceManager.getFileID ( record->getLocation() ) != sourceManager.getMainFileID() ) {
            return true;
        }
        
//         bool invalid;
//         auto sentry = sourceManager.getSLocEntry(sourceManager.getFileID ( record->getLocation() ), &invalid);
//         
//         if (!invalid) {
//             const FileEntry* entry = sourceManager.getFileEntryForSLocEntry(sentry);
//             if (entry) {
//                 std::cout << "file is: " << entry->getName() << std::endl;
//             }
//         }

        //we ignore abstract and non-classes
        if(!record->isCompleteDefinition() || (!record->isClass() && !record->isAbstract())) {
            return true;
        }
              
        std::string qualname = record->getQualifiedNameAsString();
    
        if (classMapping.count(qualname) == 0) {
            classMapping[qualname] = new ClassDefinition(record->getNameAsString(), qualname);
        }

        ClassDefinition* clazz = classMapping[qualname];
               
        bool hasConstructors = false;
        //parsing constructors
        for(auto it = record->ctor_begin(); it != record->ctor_end(); ++it) {
            if (it->getAccess() != AS_public && it->getAccess() != AS_none) {
                continue;
            }
            
            clazz->methods.push_back(createMethod<CXXConstructorDecl>(MethodDefinition::FuncType::constructor, **it, *clazz));
            hasConstructors = true;
        }
        
         //if the default constructor is undeclared, manually create one with no parameters
        if (false) {
            if (!record->hasDeclaredDefaultConstructor()) {
                MethodDefinition md;
                md.functionType = MethodDefinition::FuncType::constructor;                
                
                clazz->methods.push_back(md);
            }
        }
        
        for (auto it = record->bases_begin(); it != record->bases_end(); ++it) {
            std::string qualType = getCanonicalTypeFromQualifiedType(it->getType());
               
            if (classMapping.count(qualType) == 0) {
                classMapping[qualType] = new ClassDefinition(qualType, qualType);                
            }

            clazz->bases.insert(classMapping[qualType]);
            clazz->dependencies.insert(qualType);
        }

        //parsing methods
        for (auto method = record->method_begin(); method != record->method_end(); ++method) {
            if ( method->getAccess() != AS_public 
                || isa<CXXConstructorDecl>(*method) || isa<CXXDestructorDecl>(*method)) {
                continue;
            }
            
            clazz->methods.push_back(createMethod<CXXMethodDecl>(MethodDefinition::FuncType::method, **method, *clazz));
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
        Compiler.getDiagnostics().setSuppressAllDiagnostics(true);
        tool = new LuaBinderConsumer(Compiler.getSourceManager());
        return tool;
    }

    LuaBinderConsumer* tool;
};

gdx::JsonValue dumpType(const CxxType* type) {
    return {
        "namespace", type->ns,
        "type" , type->type,
        "spelling", type->spelling
    };    
}

gdx::JsonValue dumpParam(const MethodParameter& param) {
    gdx::JsonValue tp = dumpType(param.type);
    tp += {
      "name" , param.name,
      "is_const" , param.isConst,
      "is_pointer", param.isPointer,
      "is_ref", param.isReference
    };
    
    return tp;    
}

std::string dump(gdx::JsonValue& classDef, const ClassDefinition& def) {
    std::stringstream ss;
    
    unsigned int i = 0;
    gdx::JsonValue& jdef = classDef[def.qualifiedName];
    
    jdef["name"].as_string() = def.name;
    jdef["qualname"].as_string() = def.qualifiedName;
    
    jdef["dependencies"].as_array();
    for (const auto& dependency : def.dependencies) {
        jdef["dependencies"].at(i++).as_string() = dependency;
    }
    
    i = 0;
    jdef["bases"].as_array();
    for (const auto& dependency : def.bases) {
        jdef["bases"].at(i++).as_string() = dependency->name;
    }        
    
    i = 0;
    jdef["functions"].as_array();
    for (const auto& method : def.methods) {
        gdx::JsonValue function {
            "func_type", (method.functionType == MethodDefinition::FuncType::constructor ? "constructor" : "function"),
            "name", method.name,
            "is_virtual", method.isVirtual
        };
        
        int j = 0;
        function["params"].as_array();
        for (const auto& param: method.parameters) {
            function["params"].at(j++) = dumpParam(param);;
        }
               
        if (method.functionType != MethodDefinition::FuncType::constructor) {
            function["return"] = dumpParam(method.retType);            
        }
        
        jdef["functions"].at(i++) = function;
    }

    return ss.str();
}

int main ( int argc, const char** argv ) {
    static llvm::cl::opt<std::string> OutputPath(
       "o", llvm::cl::desc("Output file"), llvm::cl::Required);
    
    CommonOptionsParser parser( argc, argv );
    
    ClangTool tool(parser.GetCompilations(), parser.GetSourcePathList());

    int result = tool.run ( newFrontendActionFactory<BuildLuaBindingsAction>() );
       
    gdx::JsonValue json;
        
    for (const auto& cls: classMapping) {
        dump(json["classes"], *cls.second);
    }

    std::string jsonstr = json.toString();

    FILE* f = nullptr;
    f = fopen(OutputPath.c_str(), "w");
    fwrite(jsonstr.c_str(), 1, jsonstr.length(), f);

    fclose(f);

//    std::ofstream of;
//    of.open(OutputPath);
    
//    of << jsonstr;
    
//    of.close();
    
    return result;
}
