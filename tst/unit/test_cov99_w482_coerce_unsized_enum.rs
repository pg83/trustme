//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Cannot implement CoerceUnsized on non-structs
#![feature(coerce_unsized)]

use core::ops::CoerceUnsized;

enum Choice {
    Only,
}

impl CoerceUnsized<Choice> for Choice {}
