//@ compile-fail: Type mismatch
trait Value { fn read(&self) -> u16; }
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn transfer<'a>(slot: &mut &'a mut (dyn Value + Send), value: &'a mut (dyn Value + Sync)) {
    replace(slot, value);
}
fn main() {}
