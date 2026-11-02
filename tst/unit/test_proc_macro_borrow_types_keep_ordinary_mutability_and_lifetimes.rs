//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity as proxy;

#[proxy::passthrough]
fn shared<'a>(value: &'a u16) -> &'a u16 { value }

#[proxy::passthrough]
fn mutable<'a>(value: &'a mut u16) -> &'a mut u16 {
    *value += 5;
    value
}

fn main() {
    let mut value = 53u16;
    assert_eq!(*shared(&value), 53);
    assert_eq!(*mutable(&mut value), 58);
    assert_eq!(value, 58);
}
