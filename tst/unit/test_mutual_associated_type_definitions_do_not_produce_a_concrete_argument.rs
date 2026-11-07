//@ compile-fail: error:

trait Turn {
    type Left;
    type Right;
}

struct Ring;

impl Turn for Ring {
    type Left = <Self as Turn>::Right;
    type Right = <Self as Turn>::Left;
}

fn consume(_: <Ring as Turn>::Left) {}

fn main() {}
