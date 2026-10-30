#![feature(specialization, trait_alias)]
#![allow(incomplete_features)]
trait Source { type Stream; }
trait Cloned = Source<Stream: Iterator<Item: Clone>>;
trait Copied = Source<Stream: Iterator<Item: Copy>>;
struct Bytes;
impl Source for Bytes { type Stream = std::vec::IntoIter<u8>; }
struct Text;
impl Source for Text { type Stream = std::vec::IntoIter<String>; }
struct Packet<T>(T);
trait Rank { fn rank() -> u8; }
impl<T: Cloned> Rank for Packet<T> { default fn rank() -> u8 { 3 } }
impl<T: Copied> Rank for Packet<T> { fn rank() -> u8 { 7 } }
fn main() {
    assert_eq!(Packet::<Bytes>::rank(), 7);
    assert_eq!(Packet::<Text>::rank(), 3);
}
