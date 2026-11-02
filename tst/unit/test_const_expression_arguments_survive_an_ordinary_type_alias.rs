//@ compile-variant: -O
struct Packet<const N: usize>([u8; N]);
type Alias<const N: usize> = Packet<N>;
fn main() {
    let value: Alias<{ 2 + 3 }> = Packet([3, 5, 7, 11, 13]);
    assert_eq!(value.0.len(), 5);
    assert_eq!(value.0[4], 13);
}
