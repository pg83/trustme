#![feature(generic_arg_infer, inherent_associated_types)]
#![allow(incomplete_features)]

struct Shape<const N: usize>;

impl<const N: usize> Shape<N> {
    type Pair = ([u8; N], [u16; N]);
}

fn main() {
    let first: Shape<_>::Pair = ([97, 101], [487, 491]);
    let second: Shape<_>::Pair = ([103, 107, 109], [499, 503, 509]);
    assert_eq!(first.0.len(), 2);
    assert_eq!(first.1[1], 491);
    assert_eq!(second.0.len(), 3);
    assert_eq!(second.1[2], 509);
}
