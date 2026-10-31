//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
#[repr(i128)]
enum Packet { Bottom(u8) = i128::MIN, Negative(u8) = -(1i128 << 100), Top(u8) = i128::MAX }
#[inline(never)]
fn inspect(value: &Packet) -> (u8, u8) {
    match value { Packet::Bottom(number) => (3, *number), Packet::Negative(number) => (5, *number), Packet::Top(number) => (7, *number) }
}
fn main() {
    assert_eq!(inspect(&Packet::Bottom(37)), (3, 37));
    assert_eq!(inspect(&Packet::Negative(41)), (5, 41));
    assert_eq!(inspect(&Packet::Top(43)), (7, 43));
}
