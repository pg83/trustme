//@ aux-build: w452_original/cov99_w452_macro_anchor.rs
//@ aux-output-dir: w452_original/cov99_w452_macro_anchor.rs=t_anchor_orig
//@ aux-build: cov99_w452_macro_facade.rs
//@ aux-build: cov99_w452_macro_leaf.rs
//@ aux-build: w452_alternate/cov99_w452_macro_anchor.rs
//@ aux-output-dir: w452_alternate/cov99_w452_macro_anchor.rs=t_anchor_alt
//@ crate-type: lib
//@ compile-flags: --emit=metadata -L $OUT_anchor_orig
#![no_std]

pub use cov99_w452_macro_facade::Gate::{self};
