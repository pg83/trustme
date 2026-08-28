#pragma once

class HIRCrate;
struct WireBoard;

void TypecheckModuleLevel(const WireBoard& wb, HIRCrate& crate);
void TypecheckExpressions(const WireBoard& wb, HIRCrate& crate);
