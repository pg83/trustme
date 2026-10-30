//@ proc-macro-aux-build: deref_parameter_relay.rs
#![feature(deref_patterns)]
#![allow(incomplete_features)]

use deref_parameter_relay::relay;

struct Token(u8);

#[relay]
fn read(deref!(value): Box<self::Token>) -> u8 {
    value.0
}

fn main() {
    assert_eq!(read(Box::new(Token(17))), 17);
}
