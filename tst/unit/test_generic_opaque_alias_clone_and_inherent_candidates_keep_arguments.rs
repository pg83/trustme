//@ compile-fail: error:
#![feature(type_alias_impl_trait)]

type Packet<T: Clone> = impl Clone;

#[define_opaque(Packet)]
fn packet<T: Clone>(value: T) -> Packet<T> { value }

fn duplicate<T: Clone>(value: &Packet<T>) -> Packet<T> {
    <Packet<_> as Clone>::clone(value)
}

struct Inspect<T>(T);
impl Inspect<Packet<u8>> {
    fn code(&self) -> u8 { 61 }
}
impl Inspect<Packet<u16>> {
    fn code(&self) -> u8 { 67 }
}

fn main() {
    let byte = packet(71u8);
    let word = packet(73u16);
    assert_eq!(Inspect(duplicate(&byte)).code(), 61);
    assert_eq!(Inspect(duplicate(&word)).code(), 67);
}
