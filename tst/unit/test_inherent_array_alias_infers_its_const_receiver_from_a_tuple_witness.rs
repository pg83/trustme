#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Source<const N: usize>;
trait Family { type Item; }
impl Family for Source<2> { type Item = u16; }

impl<const N: usize> Source<N> where Self: Family {
    type Row<const WIDTH: usize> = (Self, [<Self as Family>::Item; WIDTH]);
}

fn main() {
    let value: Source<_>::Row<3> = (Source::<2>, [101u16, 103, 107]);
    assert_eq!(value.1, [101, 103, 107]);
    let _: Source<2> = value.0;
}
