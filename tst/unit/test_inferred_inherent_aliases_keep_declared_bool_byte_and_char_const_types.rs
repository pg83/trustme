#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Tagged<const FLAG: bool, const BYTE: u8, const LETTER: char>;
impl<const FLAG: bool, const BYTE: u8, const LETTER: char> Tagged<FLAG, BYTE, LETTER> {
    fn values(&self) -> (bool, u8, char) { (FLAG, BYTE, LETTER) }
}

struct Packet<T>(std::marker::PhantomData<T>);
impl<T> Packet<T> {
    type Item<const FLAG: bool, const BYTE: u8, const LETTER: char> =
        (T, Tagged<FLAG, BYTE, LETTER>);
}

fn main() {
    let packet: Packet<_>::Item<true, 5, 'λ'> = (91u32, Tagged);
    assert_eq!(packet.0, 91);
    assert_eq!(packet.1.values(), (true, 5, 'λ'));
}
