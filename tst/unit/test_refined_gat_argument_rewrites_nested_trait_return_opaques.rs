#![allow(refining_impl_trait)]
//@ compile-fail: error
trait Family {
    type Member<T>;
    fn tag(&self) -> u8;
}

struct Packet;

impl Family for Packet {
    type Member<T> = T;
    fn tag(&self) -> u8 { 47 }
}

trait Factory {
    fn make(&self) -> impl Family<Member<impl Copy> = u8>;
}

impl Factory for () {
    fn make(&self) -> impl Family<Member<u8> = u8> { Packet }
}

fn read<F: Factory>(factory: &F) -> u8 {
    factory.make().tag()
}

fn main() {
    assert_eq!(read(&()), 47);
}
