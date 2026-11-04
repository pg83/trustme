//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Base {
    fn amount(&self, increment: u16) -> u16;
}

trait Forward: Base {
    reuse Base::amount as forwarded;
}

struct Counter(u16);
impl Base for Counter {
    fn amount(&self, increment: u16) -> u16 { self.0 + increment }
}
impl Forward for Counter {}

fn main() {
    let value = Counter(79);
    assert_eq!(value.forwarded(4), 83);
    assert_eq!(Forward::forwarded(&value, 10), 89);
}
