#pragma once

#include <vector>
#include <utility> // std::pair

class HIRCrate;
struct WireBoard;
class HIRExprPtr;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTraitImpl;
struct HIRPattern;
class HIRItemPath;

extern void HIRExpandAnnotateUsage(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandVTables(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandClosures(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandUfcsEverything(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandReborrows(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandErasedType(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandStaticBorrowConstantsMark(const WireBoard& wb, HIRCrate& crate);
extern void HIRExpandStaticBorrowConstants(const WireBoard& wb, HIRCrate& crate);

extern void HIRExpandAnnotateUsageExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void HIRExpandClosuresExpr(const WireBoard& wb, const HIRCrate& crate, HIRTypeRef& expTy, HIRExprPtr& exp);
extern void HIRExpandUfcsEverythingExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp, const HIRTraitImpl* currentTraitImpl = nullptr);
extern void HIRExpandReborrowsExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsMarkExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
