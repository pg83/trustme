#![feature(specialization)]
trait Ready {}
impl Ready for u8 {}
impl Ready for u16 {}
struct Pair<A, B>(A, B);
trait Rank { type Value; fn rank(&self) -> u8; }
impl Rank for Pair<u8, u8> { fn rank(&self) -> u8 { 3 } }
impl<A: Ready, B: Ready> Rank for Pair<A, B> {
    default type Value = u8;
    default fn rank(&self) -> u8 { 1 }
}
impl<A: Ready> Rank for Pair<A, A> {
    default type Value = u16;
    default fn rank(&self) -> u8 { 2 }
}
fn main() {
    assert_eq!(Pair(4u8, 5u16).rank(), 1);
    assert_eq!(Pair(4u16, 5u16).rank(), 2);
    assert_eq!(Pair(4u8, 5u8).rank(), 3);
    assert_eq!(std::mem::size_of::<<Pair<u8, u8> as Rank>::Value>(), 2);
    assert_eq!(std::mem::size_of::<<Pair<u8, u16> as Rank>::Value>(), 1);
}
