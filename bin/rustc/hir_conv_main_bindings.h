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
class HIRTypeInterner;
struct HIRSimplePath;
struct HIRGenericPath;
class HIRGenericParams;
struct HIRPathParams;
class HIRConstGeneric;
class HIRArraySize;
class StaticTraitResolve;

HIRPathParams ConvertHIRCompleteAliasParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, const HIRGenericPath& path, bool isExpr);
void ConvertHIRExpandAliases(const WireBoard& wb, HIRCrate& crate);
void ConvertHIRValidateReceivers(const WireBoard& wb, HIRCrate& crate);
void ConvertHIRExpandAliasesSelf(HIRCrate& crate);
const HIRTypeData* ConvertHIRExpandAliasesSelfExpr(const HIRCrate& crate, const HIRTypeData* implType, std::vector<std::pair<HIRPattern, const HIRTypeData*>>& args, const HIRTypeData* retTy, HIRExprPtr& expr);
const HIRTypeData* ConvertHIRExpandTypeAlias(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr);
void ConvertHIRBind(const WireBoard& wb, HIRCrate& crate);
void ConvertHIRResolveUFCSSortImpls(WireBoard& wb, HIRCrate& crate);

void ConvertHIRIndexInherentMethods(const WireBoard& wb, const HIRCrate& crate);
void ConvertHIRResolveUFCSOuter(const WireBoard& wb, HIRCrate& crate);
void ConvertHIRResolveUFCS(const WireBoard& wb, HIRCrate& crate);
void ConvertHIRMarkings(const WireBoard& wb, HIRCrate& crate);

void ConvertHIRResolveUFCSExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr);
