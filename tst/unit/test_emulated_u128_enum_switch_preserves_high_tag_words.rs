//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
#[repr(u128)]
enum Packet {
    Start(u16) = 0,
    Middle(u16) = 1u128 << 100,
    End(u8) = u128::MAX,
}
#[inline(never)]
fn decode(value: &Packet) -> (u8, u16) {
    match value {
        Packet::Start(value) => (1, *value),
        Packet::Middle(value) => (2, *value),
        Packet::End(value) => (3, *value as u16),
    }
}
fn main() {
    assert_eq!(decode(&std::hint::black_box(Packet::Start(29))), (1, 29));
    assert_eq!(decode(&std::hint::black_box(Packet::Middle(1025))), (2, 1025));
    assert_eq!(decode(&std::hint::black_box(Packet::End(17))), (3, 17));
}
