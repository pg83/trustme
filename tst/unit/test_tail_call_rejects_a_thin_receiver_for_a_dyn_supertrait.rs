//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
trait Base { fn apply(&self, value: u16) -> u16; }
trait Extended: Base { fn other(&self) -> u16; }
struct Multiply(u16);
impl Base for Multiply { fn apply(&self, value: u16) -> u16 { self.0 * value } }
impl Extended for Multiply { fn other(&self) -> u16 { self.0 + 1 } }
#[inline(never)]
fn relay(receiver: &Multiply, value: u16) -> u16 {
    let receiver: &dyn Extended = std::hint::black_box(receiver);
    become Base::apply(receiver, value)
}
fn main() {
    let value = Multiply(7);
    assert_eq!(relay(&value, 11), 77);
    assert_eq!(value.other(), 8);
}
