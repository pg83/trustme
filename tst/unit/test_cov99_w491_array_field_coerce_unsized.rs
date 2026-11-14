//@ compile-fail: Handle CoerceUnsized type
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

struct ArrayField<T>([T; 1]);

impl<T, U> CoerceUnsized<ArrayField<U>> for ArrayField<T> {}

fn main() {}
