//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ proc-macro-aux-build: cov99_w301_static_lifetime_attribute.rs

extern crate cov99_w301_static_lifetime_attribute;

#[cov99_w301_static_lifetime_attribute::keep]
pub type StaticRef = &'static u8;
