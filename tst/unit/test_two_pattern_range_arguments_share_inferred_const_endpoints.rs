#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
fn second<const LOW: u16, const HIGH: u16>(_: std::pat::pattern_type!(u16 is LOW..HIGH), value: std::pat::pattern_type!(u16 is LOW..HIGH)) -> std::pat::pattern_type!(u16 is LOW..HIGH) { value }
fn main() {
    let low: std::pat::pattern_type!(u16 is 2..8) = 3;
    let high: std::pat::pattern_type!(u16 is 2..8) = 7;
    let selected = Some(second(low, high));
    let raw: u16 = unsafe { std::mem::transmute(selected.unwrap()) };
    assert_eq!(raw, 7);
}
