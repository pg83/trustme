trait Source<T> {
    type Value;
    fn value(&self) -> Self::Value;
}
//@ compile-fail: nested `impl Trait` is not allowed
trait Carrier<T>: Source<T> {}

struct Packet(u8);

impl Source<u16> for Packet {
    type Value = u8;
    fn value(&self) -> u8 { self.0 }
}

impl Carrier<u16> for Packet {}

trait Factory {
    fn make(&self) -> impl Carrier<impl Copy, Value = u8>;
}

impl Factory for () {
    fn make(&self) -> impl Carrier<impl Copy, Value = u8> { Packet(31) }
}

fn read<F: Factory>(factory: &F) -> u8 {
    factory.make().value()
}

fn main() {
    assert_eq!(read(&()), 31);
}
