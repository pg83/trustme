//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(decl_macro)]

pub macro same() {}

mod inner {
    #[macro_export]
    pub macro same() {}
}

fn main() {}
