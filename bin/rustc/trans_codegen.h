#pragma once

#include "trans_trans_list.h"
#include "trans_main_bindings.h" // TransOptions

class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRPath;
class HIRGenericPath;

class HIRFunction;
class HIRStatic;
class HIRGlobalAssembly;

class MIRFunctionPointer;

class CodeGenerator {
public:
    virtual ~CodeGenerator();

    virtual void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile);

    // Called on all types directly mentioned (e.g. variables, arguments, and fields)
    // - Inner-most types are visited first.
    virtual void emitTypeProto(const HIRTypeData*);

    virtual void emitType(const HIRTypeData*);

    virtual void emitTypeId(const HIRTypeData*);

    // Called when a ASTType*::Path is encountered (after visiting inner types)
    virtual void emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) = 0;
    virtual void emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) = 0;
    virtual void emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) = 0;

    virtual void emitConstructorEnum(const Span& sp, const HIRGenericPath& path, const HIREnum& item, size_t varIdx) = 0;
    virtual void emitConstructorStruct(const Span& sp, const HIRGenericPath& path, const HIRStruct& item) = 0;

    virtual void emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params);

    virtual void emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params);

    virtual void emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& val) = 0;

    virtual void emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params);

    virtual void emitFunctionProto(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef);

    virtual void emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) = 0;

    virtual void emitGlobalAsm(const HIRGlobalAssembly&) = 0;
};
