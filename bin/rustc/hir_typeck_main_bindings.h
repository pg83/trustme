#pragma once

class HIRCrate;

extern void TypecheckModuleLevel(HIRCrate& crate);
extern void TypecheckExpressions(HIRCrate& crate);
extern void TypecheckExpressionsValidate(HIRCrate& crate);
