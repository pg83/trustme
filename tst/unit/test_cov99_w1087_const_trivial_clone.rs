//@ crate-type: lib
//@ compile-flags: -Zunpretty=expanded -Zdump-ast -Cpanic=abort
//@ env: RUSTC_OVERRIDE_VERSION_STRING=1.93.0
#![feature(derive_const, const_trait_impl, const_clone)]

#[derive(Copy)]
#[derive_const(Clone)]
pub struct ConstCopy {
    pub value: u32,
}

pub const CLONED_VALUE: u32 = ConstCopy { value: 37 }.clone().value;
