//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// The same where-clause reaches candidate assembly twice: as a cached
// ParamEnv predicate carrying its associated equality and through the
// implied-declaration walk without it.  A bare predicate has no opinion
// on the value -- refinement, not a conflicting response.  Mirrors
// compiler-builtins float/div.rs (Shr<u32, Output = HalfRep<F>>).

use core::ops::Shr;

trait HIntB {
    type D;
}

trait FloatB {
    type Int: HIntB;
}

fn g<F: FloatB>(x: <F::Int as HIntB>::D) -> <F::Int as HIntB>::D
where
    <F::Int as HIntB>::D: Shr<u32, Output = <F::Int as HIntB>::D>,
{
    x >> 1u32
}
