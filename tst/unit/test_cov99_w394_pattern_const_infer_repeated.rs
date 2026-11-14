//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(incomplete_features)]

type Window<const END: usize> = std::pat::pattern_type!(usize is 0..=END);

trait Repeated {}
impl<T> Repeated for (T, T) {}

fn require<T: Repeated>() {}

fn main() {
    require::<(Window<_>, Window<7>)>();
}
