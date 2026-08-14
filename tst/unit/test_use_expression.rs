#![feature(ergonomic_clones)]
#![allow(incomplete_features)]

use std::clone::UseCloned;

#[derive(Clone)]
struct Token(u32);

impl UseCloned for Token {}

fn main() {
    let token = Token(7);
    let cloned = token.use;
    assert_eq!(token.0, cloned.0);

    let copied = { 20 + 1 }.use.use;
    assert_eq!(copied, 21);
}
