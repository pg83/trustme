//@ aux-build: inherent_alias_packet.rs
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

use inherent_alias_packet::Packet;

fn main() {
    let pair: Packet<u16>::Pair = Packet(601u16).duplicate();
    let item: Packet<u32>::Item = 0x1_2345u32;
    assert_eq!(pair, (601, 601));
    assert_eq!(item, 0x1_2345);
    assert_eq!(std::mem::size_of::<Packet<u16>::Pair>(), 4);
}
