#![feature(type_alias_impl_trait)]
#![allow(type_alias_bounds)]

trait Types {
    type Argument;
}

struct Word;
impl Types for Word {
    type Argument = u16;
}

struct Bytes;
impl Types for Bytes {
    type Argument = [u8; 3];
}

type Callable<T: Types> = impl Fn() -> u16;

fn source<U>() -> u16 {
    core::mem::size_of::<U>() as u16 + 641
}

#[define_opaque(Callable)]
fn make<T: Types>() -> Callable<T> {
    source::<T::Argument>
}

trait Label<T> {
    fn value(&self) -> u16;
}

impl<T: Types> Label<T> for Callable<T> {
    fn value(&self) -> u16 {
        self()
    }
}

struct Wrapper<U>(U);

impl<U: Types> Wrapper<U> {
    fn width(&self) -> usize {
        core::mem::size_of::<U::Argument>()
    }
}

trait Extract {
    type Item;
}

impl Extract for Word {
    type Item = u32;
}

trait Stamp {
    fn stamp(&self) -> usize;
}

impl<T, U: Extract<Item = T>> Stamp for Wrapper<U> {
    fn stamp(&self) -> usize {
        core::mem::size_of::<T>()
    }
}

fn main() {
    assert_eq!(<Callable<Word> as Label<Word>>::value(&make::<Word>()), 643);
    assert_eq!(<Callable<Bytes> as Label<Bytes>>::value(&make::<Bytes>()), 644);
    assert_eq!(Wrapper(Word).width(), 2);
    assert_eq!(Wrapper(Word).stamp(), 4);
}
