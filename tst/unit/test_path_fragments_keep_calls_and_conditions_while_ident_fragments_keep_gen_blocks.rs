//@ edition: 2024
#![feature(gen_blocks)]

struct Packet(u8);
const ENABLED: bool = true;

fn increment(value: u8) -> u8 {
    value + 1
}

macro_rules! call {
    ($target:path, $value:expr) => { $target($value) };
}

macro_rules! condition {
    ($value:path) => { if $value { 7 } else { 0 } };
}

macro_rules! generator {
    ($keyword:ident, $value:expr) => { $keyword move { yield $value; } };
}

fn main() {
    assert_eq!(call!(Packet, 17).0, 17);
    assert_eq!(call!(crate::increment, 23), 24);
    assert_eq!(condition!(ENABLED), 7);
    let mut values = generator!(gen, 41u8);
    assert_eq!(values.next(), Some(41));
    assert_eq!(values.next(), None);
}
