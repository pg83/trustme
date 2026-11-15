//@ aux-build: cov99_w1075_float_signature.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(fn_delegation, f16, f128)]
#![allow(incomplete_features)]

pub reuse cov99_w1075_float_signature::keep_wide as delegated_keep_wide;

pub fn use_delegated(half: f16, wide: f128) -> f128 {
    delegated_keep_wide(half, wide)
}
