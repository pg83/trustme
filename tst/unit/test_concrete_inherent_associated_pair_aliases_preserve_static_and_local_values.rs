#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Shape<const N: usize>;

impl<const N: usize> Shape<N> {
    type Pair = ([u8; N], [u16; N]);
}

const STORED: Shape<2>::Pair = ([113, 127], [521, 523]);

fn main() {
    let local: Shape<3>::Pair = ([131, 137, 139], [541, 547, 557]);
    assert_eq!(STORED.0[1], 127);
    assert_eq!(STORED.1[1], 523);
    assert_eq!(local.0[2], 139);
    assert_eq!(local.1[2], 557);
}
