//@ compile-fail: Reached the recursion limit while auto-dereferencing
use std::ops::Deref;
struct Cycle;
impl Deref for Cycle {
    type Target = Cycle;
    fn deref(&self) -> &Self { self }
}

fn main() {
    let cycle = Cycle;
    let first = std::hint::black_box(true);
    let selected = if first { &cycle } else { &17u8 };
    let _ = selected;
}
