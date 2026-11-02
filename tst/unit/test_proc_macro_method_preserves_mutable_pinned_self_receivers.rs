//@ proc-macro-aux-build: type_phase_identity.rs
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
extern crate type_phase_identity as proxy;

struct Counter(u16);

impl Counter {
    #[proxy::passthrough]
    fn bump<'a>(&'a pin mut self) -> u16 {
        let value = std::pin::Pin::get_mut(self);
        value.0 += 17;
        value.0
    }
}

fn main() {
    let mut value = Counter(43);
    let pinned = &pin mut value;
    assert_eq!(Counter::bump(pinned), 60);
    assert_eq!(value.0, 60);
}
