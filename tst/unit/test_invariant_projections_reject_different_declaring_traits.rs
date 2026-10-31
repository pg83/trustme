//@ compile-fail: Type mismatch
trait Left { type Item; }
trait Right { type Item; }
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn transfer<T: Left + Right>(slot: &mut <T as Left>::Item, value: <T as Right>::Item) {
    replace(slot, value);
}
fn main() {}
