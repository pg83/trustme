//@ edition: 2021
//@ aux-build: cov99_w496_incoherent_named_function.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(rustc_attrs)]
#![allow(internal_features, dead_code)]

extern crate cov99_w496_incoherent_named_function as source;

impl source::Shared {
    #[rustc_allow_incoherent_impl]
    fn choose() -> u8 { 2 }
}

pub fn use_hidden() -> u8 {
    source::export()()
}
