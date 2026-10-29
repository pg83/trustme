//@ aux-build: exported_nested_word_aliases.rs
#![feature(trait_alias)]
use exported_nested_word_aliases::{Input, Portable, Word};
fn narrow<T: Portable<Item = u16>>() -> u16 { <T as Word<u8>>::word() }
trait LocalMarker {}
impl LocalMarker for Input {}
trait Imported = LocalMarker + Portable<u16>;
trait Bound = Imported<Item = u32>;
fn exact_bound<T: Bound>() -> u32 { <T as Word<u16>>::word() }
fn wide<T: Imported<Item = u32>>() -> u32 { <T as Word<u16>>::word() }
fn copied<T: Portable<Item: Copy>>() -> (T::Item, T::Item) {
    let item = <T as Word<u8>>::word();
    (item, item)
}
fn opaque() -> impl Imported<Item: Copy + Into<u64>> { Input }
fn opaque_words<T: Imported<Item: Copy + Into<u64>>>(_: T) -> (u64, u64) {
    let item = <T as Word<u16>>::word();
    (item.into(), item.into())
}
fn main() {
    assert_eq!(narrow::<Input>(), 513);
    assert_eq!(wide::<Input>(), 65539);
    assert_eq!(exact_bound::<Input>(), 65539);
    assert_eq!(copied::<Input>(), (513, 513));
    assert_eq!(opaque_words(opaque()), (65539, 65539));
}
