#pragma once

namespace HIR {
    class Crate;
};

extern void TypecheckModuleLevel(::HIR::Crate& crate);
extern void TypecheckExpressions(::HIR::Crate& crate);
extern void TypecheckExpressionsValidate(::HIR::Crate& crate);
