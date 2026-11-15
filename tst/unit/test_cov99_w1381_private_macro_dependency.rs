//@ aux-build: cov99_w1381_private_origin.rs
//@ aux-build: cov99_w1381_private_bridge.rs
//@ compile-only
//@ compile-flags: --emit=metadata

#[macro_use]
extern crate cov99_w1381_private_bridge;

fn main() {}
