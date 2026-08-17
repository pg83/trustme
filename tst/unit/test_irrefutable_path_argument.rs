// A path names a variant or a unit struct, which for a one-variant type is
// every value, so it is a parameter pattern that always matches. Exhaustiveness
// decides that, not the shape of the pattern.
#![allow(dead_code)]

use std::marker::PhantomData;

enum One {
    Only,
}

fn takes_variant(One::Only: One) {}

fn takes_marker<'a>(PhantomData::<&'a u8>: PhantomData<&'a u8>) {}

fn takes_full_range(-128..=127: i8) {}

fn main() {
    takes_variant(One::Only);
    takes_marker(PhantomData);
    takes_full_range(7);
}
