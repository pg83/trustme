#![feature(specialization, trait_alias)]
#![allow(incomplete_features)]
trait Source { type Stream; }
trait Items<Value> = Source<Stream: Iterator<Item = Value>>;
struct Narrow;
impl Source for Narrow { type Stream = std::vec::IntoIter<u16>; }
struct Wide;
impl Source for Wide { type Stream = std::vec::IntoIter<u32>; }
struct Packet<T>(T);
trait Rank { fn rank() -> u8; }
impl<T: Source> Rank for Packet<T> { default fn rank() -> u8 { 2 } }
impl<T: Items<u16>> Rank for Packet<T> { fn rank() -> u8 { 6 } }
fn main() {
    assert_eq!(Packet::<Narrow>::rank(), 6);
    assert_eq!(Packet::<Wide>::rank(), 2);
}
