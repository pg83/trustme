#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Window<const LOW: u32, const HIGH: u32> = pattern_type!(u32 is LOW..=HIGH);
fn read<const LOW: u32, const HIGH: u32>(value: Window<LOW, HIGH>) -> u32 {
    unsafe { std::mem::transmute(value) }
}
fn main() {
    let first: Window<2, 8> = 5;
    let second: Window<11, 20> = 17;
    assert_eq!(read(first), 5);
    assert_eq!(read(second), 17);
}
