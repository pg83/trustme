//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml

#[inline(never)]
fn narrow_shift(value: u32, amount: u128) -> u32 {
    value << (amount & 31u128)
}

fn main() {
    assert_eq!(narrow_shift(3, 0), 3);
    assert_eq!(narrow_shift(3, 5), 96);
    assert_eq!(narrow_shift(1, (1u128 << 96) | 31), 0x8000_0000);
}
