//@ compile-flags: -Zunleash-the-miri-inside-of-you -Zmir-opt-level=0
#![feature(explicit_tail_calls)]
struct Packet<T>(T);
enum Choice { Pair(u8, u8) }
const fn packet(value: u16) -> Packet<u16> {
    let next: fn(u16) -> Packet<u16> = Packet::<u16>;
    become next(value)
}
const fn choice(first: u8, second: u8) -> Choice {
    let next: fn(u8, u8) -> Choice = Choice::Pair;
    become next(first, second)
}
const PACKET: Packet<u16> = packet(23);
const CHOICE: Choice = choice(17, 37);
fn main() {
    assert_eq!(PACKET.0, 23);
    assert!(matches!(CHOICE, Choice::Pair(17, 37)));
}
