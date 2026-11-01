//@ compile-fail: associated types cannot be used as tuple patterns
#![feature(more_qualified_paths)]
#![allow(incomplete_features)]
struct Packet(u16);
trait Family { type Product; }
struct Marker;
impl Family for Marker { type Product = Packet; }
impl Packet {
    const fn width(Self(value): Self) -> usize { value as usize }
}
const fn width(<Marker as Family>::Product(value): Packet) -> usize { value as usize }
type Buffer = [u8; Packet::width(Packet(3)) + width(Packet(2))];
fn main() {
    let buffer: Buffer = [11, 13, 17, 19, 23];
    assert_eq!(buffer.len(), 5);
    assert_eq!(buffer[4], 23);
}
