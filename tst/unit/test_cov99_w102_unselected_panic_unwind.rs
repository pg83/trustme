//@ compile-flags: -Cpanic=abort
#![feature(panic_unwind)]

extern crate panic_unwind;

fn main() {
    assert!(cfg!(panic = "abort"));
}
