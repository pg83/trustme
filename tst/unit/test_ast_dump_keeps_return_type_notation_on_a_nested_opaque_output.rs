#![feature(return_type_notation)]
//@ compile-flags: -Zdump-ast

trait Value {
    fn value(&self) -> u16;
}

trait Maker {
    fn make(&self) -> impl Value;
}

struct Factory;
struct Payload(u16);

impl Value for Payload {
    fn value(&self) -> u16 {
        self.0
    }
}

impl Maker for Factory {
    fn make(&self) -> impl Value {
        Payload(41)
    }
}

fn read<T: Maker>(maker: &T) -> u16
where
    T::make(..): Send,
{
    maker.make().value()
}

fn main() {
    assert_eq!(read(&Factory), 41);
}
