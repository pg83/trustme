//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata

pub struct Packet<const N: usize>(pub [u8; N]);

pub trait Pack<T> {
    fn pack(input: T) -> Self;
}

impl<const N: usize> Pack<[u8; N]> for Packet<N> {
    fn pack(input: [u8; N]) -> Self {
        Self(input)
    }
}

pub fn construct() -> usize {
    let packet = Packet::pack([7u8; 3]);
    packet.0.len()
}
