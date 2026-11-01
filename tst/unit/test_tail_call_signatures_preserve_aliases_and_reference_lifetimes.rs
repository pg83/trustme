#![feature(explicit_tail_calls)]
type Number = u16;
static VALUE: u8 = 31;
fn target(value: Number) -> Number { value + 7 }
fn relay(value: u16) -> u16 {
    let identity = |value: u16| value;
    let unused = async { 7u16 };
    drop(unused);
    become target(identity(value))
}
fn target_reference(value: &'static u8) -> u8 { *value }
fn relay_reference(_: &u8) -> u8 { become target_reference(&VALUE) }
fn main() {
    assert_eq!(relay(23), 30);
    assert_eq!(relay_reference(&5), 31);
}
