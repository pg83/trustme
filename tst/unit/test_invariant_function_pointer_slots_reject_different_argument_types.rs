//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn narrow(value: u16) -> u32 { value as u32 }
fn wide(value: u32) -> u32 { value }
fn main() {
    let mut function: fn(u16) -> u32 = narrow;
    replace(&mut function, wide as fn(u32) -> u32);
}
