#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
struct Packet<const N: usize>([u8; N]);
const fn width(n: usize) -> usize { n * 2 + 1 }
fn forward<const N: usize>(packet: Packet<{width(N)}>) -> Packet<{width(N)}> { packet }
impl<const OUTER: usize> Packet<OUTER> {
    fn relay<const N: usize>(packet: Packet<{width(N)}>) -> Packet<{width(N)}> { forward::<N>(packet) }
}
fn main() { assert_eq!(Packet::<11>::relay::<2>(Packet([6;5])).0, [6;5]); }
