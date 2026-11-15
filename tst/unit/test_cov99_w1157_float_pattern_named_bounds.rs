//@ compile-flags: -Zvalidate-mir
#![feature(pattern_types, pattern_type_macro)]

const LOW: f64 = 0.0;
const HIGH: f64 = 2.0;
type Window = std::pat::pattern_type!(f64 is LOW..=HIGH);

fn main() {
    let value: Window = 1.25;
    let raw: f64 = unsafe { std::mem::transmute(value) };
    assert_eq!(raw, 1.25f64);
}
