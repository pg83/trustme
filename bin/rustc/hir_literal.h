#pragma once

#include "tagged_union.h"
#include "hir_generic_ref.h"

//enum class LiteralExprOp
//{
//    // Two arguments: left, right
//    Add, Sub, Div, Mul, Mod,
//    // One argument: the value
//    Neg, Not,
//    // Takes a list of 1+ values (first is the function name, rest are arguments)
//    Call,
//    // First argument is the binding index, second is the name
//    ConstGeneric,
