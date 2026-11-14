//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Cannot implement CoerceUnsized on non-structs
//@ proc-macro-aux-build: cov99_w482_synthetic_choice.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT
#![feature(coerce_unsized)]

use core::ops::CoerceUnsized;

cov99_w482_synthetic_choice::synthetic_choice!();

struct Selector;

trait Select<const N: usize> {
    type Output;
}

impl<const N: usize> Select<N> for Selector {
    type Output = Choice;
}

impl CoerceUnsized<Choice> for <Selector as Select<{ let _ = || (); 0 }>>::Output {}
