//@ compile-fail: unused implementor that must be used
//@ compile-flags: --emit=metadata -Cpanic=abort
#![deny(unused_must_use)]

#[must_use]
trait Obligation {}

impl Obligation for u8 {}

fn obligation() -> impl Obligation {
    17u8
}

fn main() {
    obligation();
}
