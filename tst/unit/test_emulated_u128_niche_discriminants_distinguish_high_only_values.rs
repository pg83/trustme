//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
use std::num::NonZeroU128;
#[inline(never)]
fn is_empty(value: &Option<NonZeroU128>) -> bool {
    std::mem::discriminant(value) == std::mem::discriminant(&None)
}
fn main() {
    assert!(is_empty(&None));
    assert!(!is_empty(&NonZeroU128::new(37)));
    assert!(!is_empty(&NonZeroU128::new(1u128 << 100)));
    assert!(!is_empty(&NonZeroU128::new((1u128 << 100) + 41)));
}
