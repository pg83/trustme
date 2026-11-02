//@ compile-variant: -O
fn increment(value: u16) -> u16 { value + 1 }
fn main() {
    let callbacks = (increment as fn(u16) -> u16,);
    assert!(callbacks.0(17) == 18);
    assert!(callbacks.0(23) == 24);
}
