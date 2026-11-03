//@ compile-fail: error:
#![feature(type_alias_impl_trait)]
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
type Left = impl Copy;
type Right = impl Copy;
#[define_opaque(Left)]
fn left() -> Left { 73u32 }
#[define_opaque(Right)]
fn right() -> Right { 79u32 }
fn main() { require(Wrap(left()), right()); }
