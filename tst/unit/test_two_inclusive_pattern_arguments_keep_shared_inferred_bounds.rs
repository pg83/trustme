#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
fn retain<const LOW: u16, const HIGH: u16>(_: std::pat::pattern_type!(u16 is LOW..=HIGH), value: std::pat::pattern_type!(u16 is LOW..=HIGH)) -> std::pat::pattern_type!(u16 is LOW..=HIGH) { value }
fn main() {
    let first: std::pat::pattern_type!(u16 is 3..=9) = 5;
    let second: std::pat::pattern_type!(u16 is 3..=9) = 7;
    let raw: u16 = unsafe { std::mem::transmute(retain(first, second)) };
    assert_eq!(raw, 7);
}
