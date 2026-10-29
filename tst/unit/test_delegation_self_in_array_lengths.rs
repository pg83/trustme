#![feature(fn_delegation, generic_const_exprs)]
#![allow(incomplete_features)]
struct Packet<const N: usize>([u8; N]);
trait Frame: Sized {
    const WIDTH: usize;
    fn sum(bytes: [u8; Self::WIDTH]) -> usize where [(); Self::WIDTH]: {
        bytes.iter().map(|b| *b as usize).sum()
    }
    fn packet(bytes: Packet<{ Self::WIDTH }>) -> usize where [(); Self::WIDTH]: {
        bytes.0.iter().map(|b| *b as usize).sum()
    }
    fn by_size(bytes: [u8; std::mem::size_of::<Self>()]) -> usize
        where [(); std::mem::size_of::<Self>()]: {
        bytes.iter().map(|b| *b as usize).sum()
    }
}
impl Frame for u16 { const WIDTH: usize = 3; }
impl Frame for u32 { const WIDTH: usize = 5; }
reuse Frame::{sum, packet, by_size};
fn main() {
    assert_eq!(sum::<u16>([2, 3, 5]), 10);
    assert_eq!(sum::<u32>([5, 7, 11, 13, 17]), 53);
    assert_eq!(packet::<u16>(Packet([19, 23, 29])), 71);
    assert_eq!(packet::<u32>(Packet([1, 2, 3, 4, 5])), 15);
    assert_eq!(by_size::<u16>([31, 37]), 68);
    assert_eq!(by_size::<u32>([2, 3, 5, 7]), 17);
}
