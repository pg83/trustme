//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ aux-build: cov99_w1384_old/cov99_w1384_leaf.rs
//@ aux-output-dir: cov99_w1384_old/cov99_w1384_leaf.rs=old
//@ aux-build: cov99_w1384_bridge.rs
//@ aux-build: cov99_w1384_new/cov99_w1384_leaf.rs
//@ aux-output-dir: cov99_w1384_new/cov99_w1384_leaf.rs=new

#[macro_use]
extern crate cov99_w1384_bridge;

pub fn marker() -> u8 {
    cov99_w1384_bridge::marker()
}
