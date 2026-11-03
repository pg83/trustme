//@ compile-fail: Type mismatch
use std::ops::Deref;
struct Target(u16);
struct Head(Target);
impl Deref for Head {
    type Target = Target;
    fn deref(&self) -> &Target { &self.0 }
}
fn main() {
    let head = Head(Target(631));
    let mut first = Target(641);
    let second = Target(643);
    let values = [&head, &mut first, &second];
    assert_eq!(values[0].0, 631);
    assert_eq!(values[1].0, 641);
    assert_eq!(values[2].0, 643);
    let mut third = Target(647);
    let other = [&second, &head, &mut third];
    assert_eq!(other[0].0, 643);
    assert_eq!(other[1].0, 631);
    assert_eq!(other[2].0, 647);
}
