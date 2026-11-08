#![feature(fn_delegation)]

trait Source {
    fn base(&self) -> u16;
    fn plus(&self, extra: u16) -> u16;
}

trait Forward: Source {
    reuse Source::{base as forwarded_base, plus as forwarded_plus};
}

struct Value(u16);
impl Source for Value {
    fn base(&self) -> u16 { self.0 }
    fn plus(&self, extra: u16) -> u16 { self.0 + extra }
}
impl Forward for Value {}

fn main() {
    let value = Value(23);
    assert_eq!(Forward::forwarded_base(&value), 23);
    assert_eq!(Forward::forwarded_plus(&value, 6), 29);
}
