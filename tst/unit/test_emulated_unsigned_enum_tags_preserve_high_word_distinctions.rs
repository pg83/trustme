//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
#[repr(u128)]
enum Packet { First(u8) = 37, Middle(u8) = (1u128 << 100) + 37, Last(u8) = u128::MAX }
#[inline(never)]
fn inspect(value: &Packet) -> (u8, u8) {
    match value { Packet::First(number) => (3, *number), Packet::Middle(number) => (5, *number), Packet::Last(number) => (7, *number) }
}
fn main() {
    assert_eq!(inspect(&Packet::First(41)), (3, 41));
    assert_eq!(inspect(&Packet::Middle(43)), (5, 43));
    assert_eq!(inspect(&Packet::Last(47)), (7, 47));
}
