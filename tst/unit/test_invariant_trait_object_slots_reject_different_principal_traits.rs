//@ compile-fail: Type mismatch
trait First { fn read(&self) -> u16; }
trait Second { fn read(&self) -> u16; }
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn transfer<'a>(slot: &mut &'a mut dyn First, value: &'a mut dyn Second) {
    replace(slot, value);
}
fn main() {}
