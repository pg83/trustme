//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// The associated equality declared on a supertrait bound travels with the
// alias-bound candidate: `IntB: BitXor<Output = Self>` reached through the
// `type Int: IntB` declaration must answer `Output` for the rigid projection
// `F::Int`.  Mirrors libm generic/sqrt.rs (`(d1 ^ d2) & F::SIGN_MASK`).

use core::ops::{BitAnd, BitXor};

trait IntB: BitXor<Output = Self> + BitAnd<Output = Self> + Sized + Copy {}

trait FloatB {
    type Int: IntB;
    const MASK: Self::Int;
}

fn g<F: FloatB>(d1: F::Int, d2: F::Int) -> F::Int {
    (d1 ^ d2) & F::MASK
}
