//@ crate-type: lib

struct Witness;

trait Same<A, B> {}
impl<T> Same<T, T> for Witness {}

trait Outer {}
impl<A, B> Outer for (A, B) where Witness: Same<A, B> {}

fn require<T: Outer>(_: T) {}

pub fn nested_open_option_slots() {
    let left = None;
    let right = Some(7_i64);
    require((left, right));
}
