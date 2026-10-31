//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
use std::num::NonZeroU128;
#[inline(never)]
fn inspect(value: Option<NonZeroU128>) -> u128 {
    match value { Some(number) => number.get(), None => 0 }
}
fn main() {
    let high = 1u128 << 100;
    assert_eq!(inspect(NonZeroU128::new(high)), high);
    assert_eq!(inspect(NonZeroU128::new(37)), 37);
    assert_eq!(inspect(NonZeroU128::new(high + 41)), high + 41);
    assert_eq!(inspect(None), 0);
}
