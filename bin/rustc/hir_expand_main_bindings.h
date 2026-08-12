#pragma once

#include <vector>
#include <utility> // std::pair

class HIRCrate;
class HIRExprPtr;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTraitImpl;
struct HIRPattern;
class HIRItemPath;

extern void HIRExpandAnnotateUsage(HIRCrate& crate);
extern void HIRExpandVTables(HIRCrate& crate);
extern void HIRExpandClosures(HIRCrate& crate);
extern void HIRExpandUfcsEverything(HIRCrate& crate);
extern void HIRExpandReborrows(HIRCrate& crate);
extern void HIRExpandErasedType(HIRCrate& crate);
extern void HIRExpandStaticBorrowConstantsMark(HIRCrate& crate);
extern void HIRExpandStaticBorrowConstants(HIRCrate& crate);

extern void HIRExpandAnnotateUsageExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void HIRExpandClosuresExpr(const HIRCrate& crate, HIRTypeRef& expTy, HIRExprPtr& exp);
extern void HIRExpandUfcsEverythingExpr(const HIRCrate& crate, HIRExprPtr& exp, const HIRTraitImpl* currentTraitImpl = nullptr);
extern void HIRExpandReborrowsExpr(const HIRCrate& crate, HIRExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsMarkExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void HIRExpandLifetimeInfer(HIRCrate& crate);
extern void HIRExpandLifetimeInferValidate(HIRCrate& crate);
extern void HIRExpandLifetimeInferExpr(const HIRCrate& crate, const HIRItemPath& ip, const ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args, const HIRTypeData* retTy, HIRExprPtr& exp);
