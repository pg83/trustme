//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity as proxy;

#[proxy::passthrough]
#[derive(proxy::Seen, Clone, Copy, PartialEq)]
struct Packet<T> { value: T, callback: fn(), transform: fn(T) -> T }

fn callback() {}
fn transform(value: u16) -> u16 { value + 13 }

fn main() {
    let packet = Packet { value: 17u16, callback, transform };
    let copy = packet.clone();
    (copy.callback)();
    assert!(packet == copy);
    assert_eq!((copy.transform)(copy.value), 30);
}
