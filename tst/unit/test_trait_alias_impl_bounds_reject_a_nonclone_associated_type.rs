//@ compile-fail: Failed to find impl with 'rank'
#![feature(trait_alias)]
trait Provider { type Item; }
trait CloneFamily = Provider<Item: Clone>;
struct Raw;
struct Owner;
impl Provider for Owner { type Item = Raw; }
struct Wrap<T>(T);
trait Rank { fn rank() -> u8; }
impl<T: CloneFamily> Rank for Wrap<T> { fn rank() -> u8 { 5 } }
fn main() { let _ = Wrap::<Owner>::rank(); }
