//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
trait Step { fn step(&self, value: u16) -> u16; }
struct Add(u16);
impl Step for Add { fn step(&self, value: u16) -> u16 { self.0 + value } }
#[inline(never)]
fn relay(receiver: &Add, value: u16) -> u16 {
    let receiver: &dyn Step = std::hint::black_box(receiver);
    become Step::step(receiver, value)
}
fn main() {
    assert_eq!(relay(&Add(11), 17), 28);
    assert_eq!(relay(&Add(23), 31), 54);
}
