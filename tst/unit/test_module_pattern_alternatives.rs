#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
const LOW: u32 = 1;
const HIGH: u32 = 3;
type Split = pattern_type!(u32 is LOW..=HIGH | 7..=9);
struct Value { inner: Split }
fn bits(value: Split) -> u32 { unsafe { std::mem::transmute(value) } }
fn main() {
    let value = Value { inner: 8 };
    assert_eq!(bits(value.inner), 8);
    let value = Value { inner: 2 };
    assert_eq!(bits(value.inner), 2);
    let nearby = 41u32;
    let local: pattern_type!(u32 is 1..=3 | 7..=9) = 9;
    let widened: u32 = unsafe { std::mem::transmute(local) };
    assert_eq!(widened + nearby, 50);
}
