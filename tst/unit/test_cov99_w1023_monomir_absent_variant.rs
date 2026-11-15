//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir
//@ output-contains: .mir ! =0,
#![no_std]

pub enum Void {}

pub enum MaybeFlag {
    Impossible(Void),
    Flag(bool),
    Empty,
}

pub fn make(flag: bool) -> MaybeFlag {
    MaybeFlag::Flag(flag)
}
