//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Match extern type
//@ aux-build: cov99_w1500_match_extern_v1/cov99_w1500_shape.rs
//@ aux-output-dir: cov99_w1500_match_extern_v1/cov99_w1500_shape.rs=shape_v1
//@ aux-build: cov99_w1500_match_extern_holder.rs
//@ aux-build: cov99_w1500_match_extern_v2/cov99_w1500_shape.rs
//@ aux-output-dir: cov99_w1500_match_extern_v2/cov99_w1500_shape.rs=shape_v2
#![no_std]

extern crate cov99_w1500_shape;
extern crate cov99_w1500_match_extern_holder;

pub fn examine(value: &cov99_w1500_shape::T) {
    match *value {
        cov99_w1500_match_extern_holder::C => (),
        _ => (),
    }
}
