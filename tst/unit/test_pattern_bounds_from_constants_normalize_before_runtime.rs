#![feature(pattern_types, pattern_type_macro)]
const LOWER: u32 = 3;
const UPPER: u32 = 9;
fn main() {
    let value: std::pat::pattern_type!(u32 is LOWER..=UPPER) = 7;
    let raw: u32 = unsafe { std::mem::transmute(value) };
    assert_eq!(raw, 7);
    assert_eq!(core::mem::size_of_val(&value), 4);
}
