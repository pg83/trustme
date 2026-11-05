#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Family { type Item; }
struct Narrow;
struct Wide;
impl Family for Narrow { type Item = u8; }
impl Family for Wide { type Item = u16; }

struct Shelf<T>(std::marker::PhantomData<T>);
impl<T: Family> Shelf<T> {
    type Row<const N: usize> = (T, [T::Item; N]);
}

fn main() {
    let value: Shelf<_>::Row<3> = (Wide, [71u16, 73, 79]);
    assert_eq!(value.1, [71, 73, 79]);
    let _: Wide = value.0;
}
