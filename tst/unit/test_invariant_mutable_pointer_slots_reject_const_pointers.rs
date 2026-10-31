//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut value = 7u8;
    let mut slot = &mut value as *mut u8;
    let shared = &value as *const u8;
    replace(&mut slot, shared);
}
