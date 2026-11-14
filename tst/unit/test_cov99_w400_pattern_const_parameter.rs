//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]

trait Accept<T> {}

impl<T> Accept<T> for () {}

pub fn pattern_bound<const LIMIT: u32>()
where
    (): Accept<std::pat::pattern_type!(u32 is 0..=LIMIT)>,
{}
