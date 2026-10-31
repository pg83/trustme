//@ compile-fail: Type mismatch
trait Family { type Item; }
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn transfer<T: Family, U: Family>(slot: &mut T::Item, value: U::Item) {
    replace(slot, value);
}
fn main() {}
