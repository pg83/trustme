//@ compile-fail: `become` requires a direct function call
#![feature(explicit_tail_calls)]
struct Packet(u16);
enum Choice { Pair(u8, u8) }
const fn packet(value: u16) -> Packet { become Packet(value) }
const fn choice(value: u8) -> Choice { become Choice::Pair(value, 37) }
const PACKET: Packet = packet(23);
const CHOICE: Choice = choice(17);
fn main() {
    assert_eq!(PACKET.0, 23);
    assert!(matches!(CHOICE, Choice::Pair(17, 37)));
}
