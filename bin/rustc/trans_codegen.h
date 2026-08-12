#pragma once

#include "trans_trans_list.h"
#include "trans_main_bindings.h" // TransOptions

namespace HIR {
    class TypeData;
    using TypeRef = const TypeData*;
    class Path;
    class GenericPath;

    class Function;
    class Static;
    class GlobalAssembly;
}

namespace MIR {
    class FunctionPointer;
}

class CodeGenerator {
public:
    virtual ~CodeGenerator();

    virtual void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) {
    }

    // Called on all types directly mentioned (e.g. variables, arguments, and fields)
    // - Inner-most types are visited first.
    virtual void emitTypeProto(const ::HIR::TypeData*) {
    }

    virtual void emitType(const ::HIR::TypeData*) {
    }

    virtual void emitTypeId(const ::HIR::TypeData*) {
    }

    // Called when a TypeRef::Path is encountered (after visiting inner types)
    virtual void emitStruct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) = 0;
    virtual void emitUnion(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Union& item) = 0;
    virtual void emitEnum(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Enum& item) = 0;

    virtual void emitConstructorEnum(const Span& sp, const ::HIR::GenericPath& path, const ::HIR::Enum& item, size_t varIdx) = 0;
    virtual void emitConstructorStruct(const Span& sp, const ::HIR::GenericPath& path, const ::HIR::Struct& item) = 0;

    virtual void emitStaticExt(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) {
    }

    virtual void emitStaticProto(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) {
    }

    virtual void emitStaticLocal(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params, const EncodedLiteral& val) = 0;

    virtual void emitFunctionExt(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) {
    }

    virtual void emitFunctionProto(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool isExternDef) {
    }

    virtual void emitFunctionCode(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool isExternDef, const ::MIR::FunctionPointer& code) = 0;

    virtual void emitGlobalAsm(const ::HIR::GlobalAssembly&) = 0;
};

extern ::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorC(const ::HIR::Crate& crate, const ::std::string& outfile);
extern ::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const ::HIR::Crate& crate, const ::std::string& outfile);
