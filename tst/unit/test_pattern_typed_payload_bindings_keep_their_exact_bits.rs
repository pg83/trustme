#![feature(pattern_types, pattern_type_macro)]
type Digit = std::pat::pattern_type!(u8 is 1..=9);
enum Packet { Value(Digit), Empty }
#[inline(never)]
fn inspect(value: Packet) -> u8 {
    match value { Packet::Value(digit) => unsafe { std::mem::transmute(digit) }, Packet::Empty => 0 }
}
fn main() {
    assert_eq!(inspect(Packet::Value(unsafe { std::mem::transmute(7u8) })), 7);
    assert_eq!(inspect(Packet::Empty), 0);
}
