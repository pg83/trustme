//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn constant<T: Copy>(value: T) -> impl Fn() -> T { move || value }
fn main() {
    let mut closure = constant(13u16);
    replace(&mut closure, constant(17u32));
}
