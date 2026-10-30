#![feature(specialization, trait_alias)]
#![allow(incomplete_features)]
trait Family { type Member<T>; }
trait CloneByte = Family<Member<u8>: Clone>;
trait CopyByte = Family<Member<u8>: Copy>;
struct Copied;
impl Family for Copied { type Member<T> = std::marker::PhantomData<T>; }
struct Cloned;
impl Family for Cloned { type Member<T> = Vec<T>; }
struct Wrap<T>(T);
trait Rank { fn rank() -> u8; }
impl<T: CloneByte> Rank for Wrap<T> { default fn rank() -> u8 { 4 } }
impl<T: CopyByte> Rank for Wrap<T> { fn rank() -> u8 { 9 } }
fn main() {
    assert_eq!(Wrap::<Copied>::rank(), 9);
    assert_eq!(Wrap::<Cloned>::rank(), 4);
}
