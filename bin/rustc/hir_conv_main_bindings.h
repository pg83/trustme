#pragma once

#include <vector>
#include <utility>

struct Span;

class HIRCrate;
struct WireBoard;
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
class StaticTraitResolve;

extern HIRPathParams ConvertHIRCompleteAliasParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, const HIRGenericPath& path, bool isExpr);
extern void ConvertHIRExpandAliases(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRValidateReceivers(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRExpandAliasesSelf(HIRCrate& crate);
extern void ConvertHIRExpandAliasesSelfExpr(const HIRCrate& crate, const HIRTypeData* implType, std::vector<std::pair<HIRPattern, HIRTypeRef>>& args, HIRTypeRef& retTy, HIRExprPtr& expr);
extern HIRTypeRef ConvertHIRExpandTypeAlias(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr);
extern void ConvertHIRBind(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRResolveUFCSSortImpls(WireBoard& wb, HIRCrate& crate);

extern void ConvertHIRIndexInherentMethods(const WireBoard& wb, const HIRCrate& crate);
extern void ConvertHIRResolveUFCSOuter(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRResolveUFCS(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRMarkings(const WireBoard& wb, HIRCrate& crate);
extern void ConvertHIRConstantEvaluate(const WireBoard& wb, HIRCrate& hirCrate);

extern void ConvertHIRResolveUFCSExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr);
extern void ConvertHIRConstantEvaluateExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void ConvertHIRConstantEvaluateEnum(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm);
extern void ConvertHIRConstantEvaluateEnumVariant(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm, size_t idx);
extern void ConvertHIRConstantEvaluateConstant(const StaticTraitResolve& callerResolve, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRConstant& e);
extern void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* paramsDef, HIRPathParams& params);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRTypeData* ty, HIRConstGeneric& cg);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRConstGeneric& cg);
extern void ConvertHIRConstantEvaluateArraySize(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRSimplePath& path, HIRArraySize& size);
