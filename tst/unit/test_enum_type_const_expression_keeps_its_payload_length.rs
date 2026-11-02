//@ compile-variant: -O
enum Packet<const N: usize> { Bytes([u8; N]), Empty }
fn count(value: Packet<{ 2 + 3 }>) -> usize {
    match value { Packet::Bytes(bytes) => bytes.len(), Packet::Empty => 0 }
}
fn main() {
    assert_eq!(count(Packet::Bytes([1, 2, 3, 4, 5])), 5);
    assert_eq!(count(Packet::Empty), 0);
}
