//@ aux-build: cov99_w1657_private_macro_aux.rs
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata

use cov99_w1657_private_macro_aux::CHOICE;

pub const VALUE: u8 = CHOICE;
