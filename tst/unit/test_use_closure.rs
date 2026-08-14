#![feature(ergonomic_clones)]
#![allow(incomplete_features)]

use std::clone::UseCloned;

#[derive(Clone)]
struct Token(u32);

impl UseCloned for Token {}

fn main() {
    let token = Token(7);
    let first = use || token;
    let second = use || token;

    assert_eq!(first().0, 7);
    assert_eq!(second().0, 7);
    assert_eq!(token.0, 7);
}
