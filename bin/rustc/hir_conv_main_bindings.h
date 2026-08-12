#pragma once

#include <utility>
#include <vector>

struct Span;

    class HIRCrate;
    class HIRItemPath;
    class HIRExprPtr;
    struct HIRPattern;
    class HIREnum;
    class HIRConstant;
    class HIRTypeData;
    using HIRTypeRef = const HIRTypeData*;
    class HIRTypeInterner;
    struct HIRSimplePath;
    struct HIRGenericPath;
    class HIRGenericParams;
    struct HIRPathParams;
    class HIRConstGeneric;
    class HIRArraySize;

extern void ConvertHIRLifetimeElision(HIRCrate& crate);
extern HIRPathParams ConvertHIRCompleteAliasParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, const HIRGenericPath& path, bool isExpr);
extern void ConvertHIRExpandAliases(HIRCrate& crate);
extern void ConvertHIRExpandAliasesSelf(HIRCrate& crate);
extern void ConvertHIRExpandAliasesSelfExpr(
    const HIRCrate& crate,
    const HIRTypeData* implType,
    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args,
    HIRTypeRef& retTy,
    HIRExprPtr& expr
    );
extern void ConvertHIRBind(HIRCrate& crate);
extern void ConvertHIRResolveUFCSSortImpls(HIRCrate& crate);
extern void ConvertHIRResolveUFCSOuter(HIRCrate& crate);
extern void ConvertHIRResolveUFCS(HIRCrate& crate);
extern void ConvertHIRMarkings(HIRCrate& crate);
extern void ConvertHIRConstantEvaluate(HIRCrate& hirCrate);

extern void ConvertHIRResolveUFCSExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr);
extern void ConvertHIRConstantEvaluateExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void ConvertHIRConstantEvaluateEnum(const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm);
extern void ConvertHIRConstantEvaluateConstant(const HIRCrate& crate, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRConstant& e);
extern void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const HIRCrate& crate, const HIRSimplePath& modPath, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRGenericParams* paramsDef, HIRPathParams& params);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const HIRCrate& crate, const HIRTypeData* ty, HIRConstGeneric& cg);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const HIRCrate& crate, HIRConstGeneric& cg);
extern void ConvertHIRConstantEvaluateArraySize(const Span& sp, const HIRCrate& crate, const HIRSimplePath& path, HIRArraySize& size);
