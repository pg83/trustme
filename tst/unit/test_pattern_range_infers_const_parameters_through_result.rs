#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(internal_features, incomplete_features)]
use std::pat::pattern_type;
type Window<const LOW: u16, const HIGH: u16> = pattern_type!(u16 is LOW..=HIGH);
struct Tagged<const LOW: u16, const HIGH: u16>(Window<LOW, HIGH>);
impl<const LOW: u16, const HIGH: u16> Tagged<LOW, HIGH> {
    fn endpoints(&self) -> (u16, u16) { (LOW, HIGH) }
    fn value(self) -> u16 { unsafe { std::mem::transmute(self.0) } }
}
fn tag<const LOW: u16, const HIGH: u16>(value: Window<LOW, HIGH>) -> Tagged<LOW, HIGH> { Tagged(value) }
fn main() {
    let first: Window<2, 8> = 5;
    let second: Window<11, 20> = 17;
    let first = tag(first);
    let second = tag(second);
    assert_eq!(first.endpoints(), (2, 8));
    assert_eq!(second.endpoints(), (11, 20));
    assert_eq!((first.value(), second.value()), (5, 17));
}
