//@ edition: 2015
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ aux-build: cov99_w1192_exported_alias_aux.rs
#![feature(trait_alias)]
#![allow(bare_trait_objects)]

extern crate cov99_w1192_exported_alias_aux;

fn main() {
    let value = cov99_w1192_exported_alias_aux::alias_answer!(7_u8);
    assert_eq!(value, 7);
}
