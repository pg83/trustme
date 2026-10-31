//@ compile-fail: Union patterns require named fields
#![feature(more_qualified_paths)]
#![allow(incomplete_features)]
union Bits { word: u32 }
trait Family { type Output; }
struct Tag;
impl Family for Tag { type Output = Bits; }
fn main() {
    let inspect = |<Tag as Family>::Output(word)| word;
    let _ = inspect(Bits { word: 17 });
}
