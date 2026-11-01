//@ compile-variant: -O

enum Packet<T> { Empty, Pair(T, (u16, u8)), Array([T; 3]) }
#[inline(never)]
fn score(packet: Packet<u16>) -> u16 {
    match packet {
        Packet::Empty => 3,
        Packet::Pair(value, (left, right)) => value + left * right as u16,
        Packet::Array(values) => values[0] + values[1] * 3 + values[2] * 5,
    }
}
fn main() {
    assert_eq!(score(Packet::Empty), 3);
    assert_eq!(score(Packet::Pair(7, (11, 2))), 29);
    assert_eq!(score(Packet::Array([3, 5, 7])), 53);
}
