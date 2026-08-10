// Extracted from src/expressions/operator-expr.md:1133
use core::{num::Wrapping, ops::AddAssign};

trait Equate {}
impl<T> Equate for (T, T) {}

fn f1(x: (u8,)) {
    let mut order = vec![];
    // The RHS is evaluated first as both operands are of primitive
    // type.
    { order.push(2); x }.0 += { order.push(1); x }.0;
    assert!(order.is_sorted());
}

fn f2(x: (Wrapping<u8>,)) {
    let mut order = vec![];
    // The LHS is evaluated first as `Wrapping<_>` is not a primitive
    // type.
    { order.push(1); x }.0 += { order.push(2); (0u8,) }.0;
    assert!(order.is_sorted());
}

fn f3<T: AddAssign<u8> + Copy>(x: (T,)) where (T, u8): Equate {
    let mut order = vec![];
    // The LHS is evaluated first as one of the operands is a generic
    // parameter, even though that generic parameter can be unified
    // with a primitive type due to the where clause bound.
    { order.push(1); x }.0 += { order.push(2); (0u8,) }.0;
    assert!(order.is_sorted());
}

fn main() {
    f1((0u8,));
    f2((Wrapping(0u8),));
    // We supply a primitive type as the generic argument, but this
    // does not affect the evaluation order in `f3` when
    // monomorphized.
    f3::<u8>((0u8,));
}
