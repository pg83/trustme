//@ aux-build: stalled_pattern_lenses.rs
//@ compile-fail: error:
#![feature(deref_patterns)]
#![allow(incomplete_features)]
extern crate stalled_pattern_lenses as lenses;

fn main() {
    match lenses::pair() {
        deref!((_, _)) => (),
        _ => (),
    }
}
