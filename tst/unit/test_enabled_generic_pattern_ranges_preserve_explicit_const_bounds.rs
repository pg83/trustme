#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
fn identity<const LOW: u32, const HIGH: u32>(value: std::pat::pattern_type!(u32 is LOW..=HIGH)) -> std::pat::pattern_type!(u32 is LOW..=HIGH) { value }
fn main() {
    let value: std::pat::pattern_type!(u32 is 5..=11) = 7;
    let selected = identity::<5, 11>(value);
    let raw: u32 = unsafe { std::mem::transmute(selected) };
    assert_eq!(raw, 7);
}
