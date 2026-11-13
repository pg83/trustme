//@ crate-type: lib

struct Witness;

trait Same<A, B> {}
impl<T> Same<T, T> for Witness {}

trait Outer {}
impl<A, B> Outer for (A, B) where Witness: Same<A, B> {}

fn require<T: Outer>(_: T) {}

pub fn nested_same_literal_slots() {
    require((0, 0));
}
