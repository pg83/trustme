//@ compile-variant: -O
#![feature(f128)]
#[inline(never)]
fn choose(flag: bool) -> f128 {
    let result;
    if flag { result = 1.0f128; }
    else { result = 1.0000000000000000000000000000000002f128; }
    result
}
fn main() {
    let base = 0x3fff0000000000000000000000000000u128;
    assert_eq!(choose(std::hint::black_box(true)).to_bits(), base);
    assert_eq!(choose(std::hint::black_box(false)).to_bits(), base + 1);
}
