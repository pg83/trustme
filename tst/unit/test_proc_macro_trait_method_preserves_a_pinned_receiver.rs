//@ proc-macro-aux-build: type_phase_identity.rs
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
extern crate type_phase_identity as proxy;

trait Read {
    #[proxy::passthrough]
    fn read(&pin const self) -> u16;
}

struct Counter(u16);
impl Read for Counter {
    fn read(&pin const self) -> u16 { std::pin::Pin::get_ref(self).0 }
}

fn main() {
    let value = Counter(59);
    assert_eq!(Read::read(&pin const value), 59);
}
