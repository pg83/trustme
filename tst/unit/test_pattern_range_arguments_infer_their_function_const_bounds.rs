#![feature(pattern_types, pattern_type_macro)]
fn identity<const LOW: u32, const HIGH: u32>(value: std::pat::pattern_type!(u32 is LOW..=HIGH)) -> std::pat::pattern_type!(u32 is LOW..=HIGH) {
    value
}
fn main() {
    let value: std::pat::pattern_type!(u32 is 3..=9) = 7;
    let selected = identity(value);
    let raw: u32 = unsafe { std::mem::transmute(selected) };
    assert_eq!(raw, 7);
}
