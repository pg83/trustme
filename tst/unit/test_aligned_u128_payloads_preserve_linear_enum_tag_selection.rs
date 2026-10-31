enum Packet { First(u128), Second(u128) }
#[inline(never)]
fn inspect(value: &Packet) -> (u8, u128) {
    match value { Packet::First(number) => (3, *number), Packet::Second(number) => (5, *number) }
}
fn main() {
    let first = (1u128 << 100) + 37;
    let second = u128::MAX - 41;
    assert_eq!(inspect(&Packet::First(first)), (3, first));
    assert_eq!(inspect(&Packet::Second(second)), (5, second));
}
