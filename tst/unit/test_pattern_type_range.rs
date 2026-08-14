// compile-flags: -Zvalidate-mir
#![feature(pattern_types, pattern_type_macro)]

use std::pat::pattern_type;

type Positive = pattern_type!(u32 is 1..);

trait Marker {}
impl Marker for Positive {}

fn require_marker<T: Marker>(_: T) {}

fn main() {
    let value: Positive = unsafe { std::mem::transmute(42_u32) };
    require_marker(value);
    assert_eq!(
        std::mem::size_of::<Option<Positive>>(),
        std::mem::size_of::<u32>(),
    );
}
