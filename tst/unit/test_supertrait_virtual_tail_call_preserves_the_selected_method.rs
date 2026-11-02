#![feature(explicit_tail_calls)]

trait Base { fn apply(&self, value: u16) -> u16; }
trait Extended: Base { fn other(&self) -> u16; }
struct Multiply(u16);
impl Base for Multiply { fn apply(&self, value: u16) -> u16 { self.0 * value } }
impl Extended for Multiply { fn other(&self) -> u16 { self.0 + 1 } }

fn relay(receiver: &dyn Extended, value: u16) -> u16 {
    become Base::apply(receiver, value)
}

fn main() {
    let value = Multiply(7);
    let receiver: &dyn Extended = &value;
    assert_eq!(relay(receiver, 11), 77);
    assert_eq!(receiver.other(), 8);
}
