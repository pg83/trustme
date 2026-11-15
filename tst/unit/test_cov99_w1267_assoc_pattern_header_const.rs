//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#[derive(PartialEq, Eq)]
struct Unit;

impl Unit {
    const ONLY: Self = Self;
}

trait ArrayUnit {
    type Item;
}

impl ArrayUnit for [u8; 1] {
    type Item = Unit;
}

fn a_force(<<[u8; z_length(Unit)] as ArrayUnit>::Item>::ONLY: Unit) {}

const fn z_length(Unit::ONLY: Unit) -> usize {
    1
}

fn main() {
    a_force(Unit);
}
