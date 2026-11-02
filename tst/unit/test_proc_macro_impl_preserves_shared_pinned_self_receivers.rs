//@ proc-macro-aux-build: type_phase_identity.rs
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
extern crate type_phase_identity as proxy;

struct Counter(u16);

#[proxy::passthrough]
impl Counter {
    fn read(&pin const self) -> u16 {
        let pinned: std::pin::Pin<&Self> = self;
        pinned.get_ref().0
    }
}

fn main() {
    let value = Counter(41);
    let pinned = &pin const value;
    assert_eq!(Counter::read(pinned), 41);
}
