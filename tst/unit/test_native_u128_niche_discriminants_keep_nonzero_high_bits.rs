use std::num::NonZeroU128;
enum Packet { Value(NonZeroU128), Empty }
#[inline(never)]
fn different(first: &Packet, second: &Packet) -> bool {
    std::mem::discriminant(first) != std::mem::discriminant(second)
}
fn main() {
    let low = Packet::Value(NonZeroU128::new(37).unwrap());
    let high = Packet::Value(NonZeroU128::new(1u128 << 100).unwrap());
    assert!(!different(&low, &high));
    assert!(different(&low, &Packet::Empty));
    assert!(different(&high, &Packet::Empty));
}
