#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Marker {}

struct Arithmetic<const INPUT: usize, const OUTPUT: usize>;

impl<const INPUT: usize> Marker for Arithmetic<INPUT, { INPUT + 1 }> {}

struct Cast<const INPUT: usize, const OUTPUT: u128>;

impl<const INPUT: usize> Marker for Cast<INPUT, { INPUT as u128 }> {}

fn assert_marker<T: Marker>() {}

fn main() {
    assert_marker::<Arithmetic<4, 5>>();
    assert_marker::<Cast<6, 6>>();
}
