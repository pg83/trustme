//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn identity<T>(value: T) -> T { value }
fn main() {
    let mut function = identity::<u16>;
    replace(&mut function, identity::<u32>);
}
