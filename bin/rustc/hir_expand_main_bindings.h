#pragma once

#include <std/lib/vector.h>

#include <vector>
#include <utility>

class HIRCrate;
struct WireBoard;
class HIRExprPtr;
class HIRType;
class HIRTraitImpl;
struct HIRPattern;
class HIRItemPath;
class HIRExprNode;
class Span;
class HIRExprState;

struct HIRAnnotateTypeView {
    virtual const HIRType* type(const HIRType* type) const = 0;
    virtual bool typeIsCopy(const Span& sp, const HIRType* type) const = 0;
};

void HIRExpandAnnotateUsage(const WireBoard& wb, HIRCrate& crate);
void HIRExpandVTables(const WireBoard& wb, HIRCrate& crate);
void HIRExpandClosures(const WireBoard& wb, HIRCrate& crate);
void HIRExpandUfcsEverything(const WireBoard& wb, HIRCrate& crate);
void HIRExpandReborrows(const WireBoard& wb, HIRCrate& crate);
void HIRExpandErasedType(const WireBoard& wb, HIRCrate& crate);
void HIRExpandStaticBorrowConstantsMark(const WireBoard& wb, HIRCrate& crate);
void HIRExpandStaticBorrowConstants(const WireBoard& wb, HIRCrate& crate);

void HIRExpandAnnotateUsageExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
void HIRExpandAnalyseClosureCaptures(const WireBoard& wb, const HIRExprState& state, const stl::Vector<HIRExprNode*>& closures, const stl::Vector<const HIRType*>& variableTypes, const HIRAnnotateTypeView& view);
const HIRType* HIRExpandClosuresExpr(const WireBoard& wb, HIRCrate& crate, const HIRType* expTy, HIRExprPtr& exp);
void HIRExpandUfcsEverythingExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp, const HIRTraitImpl* currentTraitImpl = nullptr);
void HIRExpandReborrowsExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp);
void HIRExpandStaticBorrowConstantsMarkExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
void HIRExpandStaticBorrowConstantsExpr(const WireBoard& wb, HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
