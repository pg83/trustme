//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn first(value: u16) -> u16 { value + 1 }
fn second(value: u16) -> u16 { value + 2 }
fn main() {
    let mut function = first;
    replace(&mut function, second);
}
