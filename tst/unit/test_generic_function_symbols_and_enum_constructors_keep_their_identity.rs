//@ compile-flags: -Zmir-opt-level=0

mod first {
    pub fn value<T: Copy>(value: T) -> (T, u16) { (value, 61) }
}

mod second {
    pub fn value<T: Copy>(value: T) -> (T, u16) { (value, 67) }
}

enum Packet<T> {
    Data(T),
    Empty,
}

#[inline(never)]
fn apply(function: fn(u16) -> (u16, u16), value: u16) -> (u16, u16) {
    function(value)
}

#[inline(never)]
fn construct(function: fn(u16) -> Packet<u16>, value: u16) -> Packet<u16> {
    function(value)
}

fn main() {
    assert_eq!(apply(first::value::<u16>, 71), (71, 61));
    assert_eq!(apply(second::value::<u16>, 73), (73, 67));
    assert_eq!(first::value::<u32>(79), (79, 61));
    match construct(Packet::<u16>::Data, 83) {
        Packet::Data(value) => assert_eq!(value, 83),
        Packet::Empty => panic!(),
    }
    match Packet::<u16>::Empty {
        Packet::Empty => {},
        Packet::Data(_) => panic!(),
    }
}
