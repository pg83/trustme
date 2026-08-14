#![feature(fn_delegation)]
#![allow(incomplete_features)]

fn source(value: u32) -> u32 {
    value + 1
}

reuse source as delegated;

fn main() {
    assert_eq!(delegated(41), 42);
}
