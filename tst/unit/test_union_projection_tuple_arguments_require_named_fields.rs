//@ compile-fail: Union patterns require named fields
#![feature(more_qualified_paths)]
#![allow(incomplete_features)]
union Bits { word: u32 }
trait Family { type Output; }
struct Tag;
impl Family for Tag { type Output = Bits; }
fn inspect(<Tag as Family>::Output(word): Bits) -> u32 { word }
fn main() { let _ = inspect(Bits { word: 13 }); }
