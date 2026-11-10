trait Family {
    type Member<T>;
    fn tag(&self) -> u8;
}
//@ compile-fail: error
struct Packet;

impl Family for Packet {
    type Member<T> = T;
    fn tag(&self) -> u8 { 43 }
}

trait Factory {
    fn make(&self) -> impl Family<Member<impl Copy> = u8>;
}

impl Factory for () {
    fn make(&self) -> impl Family<Member<impl Copy> = u8> { Packet }
}

fn read<F: Factory>(factory: &F) -> u8 {
    factory.make().tag()
}

fn main() {
    assert_eq!(read(&()), 43);
}
