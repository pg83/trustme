//@ compile-flags: -Znext-solver=globally
#![feature(specialization)]
#![allow(incomplete_features)]
struct Decoder;
trait Decode<Input> { type Output; fn decode(input: Input) -> Self::Output; }
impl<T> Decode<T> for Decoder {
    default type Output = u8;
    default fn decode(_: T) -> Self::Output { panic!("unselected parent") }
}
impl Decode<u8> for Decoder { fn decode(input: u8) -> u8 { input + 1 } }
impl Decode<u16> for Decoder { fn decode(input: u16) -> u8 { input as u8 + 2 } }
impl Decode<i32> for Decoder { fn decode(input: i32) -> u8 { input as u8 + 3 } }
fn decode<T>(input: T) -> <Decoder as Decode<T>>::Output where Decoder: Decode<T> {
    Decoder::decode(input)
}
fn main() {
    let answer: u8 = decode(37);
    assert_eq!(answer, 40);
    assert_eq!(decode(5u8), 6);
    assert_eq!(decode(7u16), 9);
}
