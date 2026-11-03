#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Short = pattern_type!(u16 is 5..=11);
type Word = pattern_type!(u32 is 17..=23);
type Long = pattern_type!(u64 is 31..=37);
type Index = pattern_type!(usize is 41..=47);
fn main() {
    assert_eq!(std::mem::size_of::<Option<Short>>(), 2);
    assert_eq!(std::mem::size_of::<Option<Word>>(), 4);
    assert_eq!(std::mem::size_of::<Option<Long>>(), 8);
    assert_eq!(std::mem::size_of::<Option<Index>>(), std::mem::size_of::<usize>());
    let short: Option<Short> = Some(7);
    let word: Option<Word> = Some(19);
    let long: Option<Long> = Some(33);
    let index: Option<Index> = Some(43);
    assert_eq!(unsafe { std::mem::transmute::<Short, u16>(short.unwrap()) }, 7);
    assert_eq!(unsafe { std::mem::transmute::<Word, u32>(word.unwrap()) }, 19);
    assert_eq!(unsafe { std::mem::transmute::<Long, u64>(long.unwrap()) }, 33);
    assert_eq!(unsafe { std::mem::transmute::<Index, usize>(index.unwrap()) }, 43);
    assert!(Option::<Long>::None.is_none());
}
