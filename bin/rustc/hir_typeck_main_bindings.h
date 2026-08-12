#pragma once

class HIRCrate;
struct WireBoard;

extern void TypecheckModuleLevel(const WireBoard& wb, HIRCrate& crate);
extern void TypecheckExpressions(const WireBoard& wb, HIRCrate& crate);
extern void TypecheckExpressionsValidate(const WireBoard& wb, HIRCrate& crate);
