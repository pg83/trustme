#pragma once

#include <vector>
#include <utility>

class HIRCrate;
struct WireBoard;
class HIRExprPtr;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTraitImpl;
struct HIRPattern;
class HIRItemPath;

void HIRExpandAnnotateUsage(const WireBoard& wb, HIRCrate& crate);
void HIRExpandVTables(const WireBoard& wb, HIRCrate& crate);
void HIRExpandClosures(const WireBoard& wb, HIRCrate& crate);
void HIRExpandUfcsEverything(const WireBoard& wb, HIRCrate& crate);
void HIRExpandReborrows(const WireBoard& wb, HIRCrate& crate);
void HIRExpandErasedType(const WireBoard& wb, HIRCrate& crate);
void HIRExpandStaticBorrowConstantsMark(const WireBoard& wb, HIRCrate& crate);
void HIRExpandStaticBorrowConstants(const WireBoard& wb, HIRCrate& crate);

void HIRExpandAnnotateUsageExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
void HIRExpandClosuresExpr(const WireBoard& wb, const HIRCrate& crate, HIRTypeRef& expTy, HIRExprPtr& exp);
void HIRExpandUfcsEverythingExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp, const HIRTraitImpl* currentTraitImpl = nullptr);
void HIRExpandReborrowsExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp);
void HIRExpandStaticBorrowConstantsMarkExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
void HIRExpandStaticBorrowConstantsExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
