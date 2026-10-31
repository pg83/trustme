//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
use std::num::NonZeroI128;
enum Packet { Value(NonZeroI128), Empty }
#[inline(never)]
fn is_empty(value: &Packet) -> bool {
    std::mem::discriminant(value) == std::mem::discriminant(&Packet::Empty)
}
fn main() {
    assert!(is_empty(&Packet::Empty));
    for number in [i128::MIN, -1, 1i128 << 100] {
        assert!(!is_empty(&Packet::Value(NonZeroI128::new(number).unwrap())));
    }
}
