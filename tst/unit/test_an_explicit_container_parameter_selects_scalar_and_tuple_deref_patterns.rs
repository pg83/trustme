//@ aux-build: stalled_pattern_lenses.rs
//@ compile-variant: -O
#![feature(deref_patterns)]
#![allow(incomplete_features)]
extern crate stalled_pattern_lenses as lenses;

fn main() {
    assert!(matches!(lenses::scalar::<u16>(), deref!(17)));
    let sum = match lenses::pair::<u8>() {
        deref!((head, tail)) => head + tail,
        _ => 0,
    };
    assert_eq!(sum, 52);
}
