//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
trait Step { fn step(&mut self, value: u16) -> u16; }
struct Counter(u16);
impl Step for Counter {
    fn step(&mut self, value: u16) -> u16 { self.0 += value; self.0 }
}
#[inline(never)]
fn relay(receiver: &mut Counter, value: u16) -> u16 {
    let receiver: &mut dyn Step = std::hint::black_box(receiver);
    become Step::step(receiver, value)
}
fn main() {
    let mut value = Counter(13);
    assert_eq!(relay(&mut value, 17), 30);
    assert_eq!(relay(&mut value, 19), 49);
    assert_eq!(value.0, 49);
}
