//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn narrow(value: u16) -> u16 { value }
fn wide(value: u16) -> u32 { value as u32 }
fn main() {
    let mut function: fn(u16) -> u16 = narrow;
    replace(&mut function, wide as fn(u16) -> u32);
}
