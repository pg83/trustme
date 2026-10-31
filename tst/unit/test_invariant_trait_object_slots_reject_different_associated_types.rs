//@ compile-fail: Type mismatch
trait Value { type Item; }
fn replace<'a>(slot: &mut &'a mut dyn Value<Item = u16>, value: &'a mut dyn Value<Item = u32>) {
    *slot = value;
}
fn main() {}
