//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut value = 11u8;
    let mut slot = &mut value;
    replace(&mut slot, &13u8);
}
