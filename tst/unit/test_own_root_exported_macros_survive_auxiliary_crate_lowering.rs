//@ crate-type: lib
//@ aux-build: own_root_macro_codec.rs
//@ input: aux/own_root_macro_codec.rs
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only

extern crate own_root_macro_codec as scores;

const TOTAL: u16 = scores::add_six!(9u16);
const _: [(); 15] = [(); TOTAL as usize];

scores::define_score!(PUBLISHED);
const _: [(); 19] = [(); PUBLISHED as usize];

pub const VALUES: (u16, u16) = (TOTAL, PUBLISHED);
