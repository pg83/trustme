#![feature(pattern_types, pattern_type_macro)]

trait Element {
    type Value;
}

struct Word;
impl Element for Word {
    type Value = u16;
}

type Present<T: Element> = std::pat::pattern_type!(*const <T as Element>::Value is !null);

trait Address<T: Element> {
    fn address(self) -> *const T::Value;
}

impl<T: Element> Address<T> for Present<T> {
    fn address(self) -> *const T::Value {
        unsafe { std::mem::transmute(self) }
    }
}

fn main() {
    let value = 467u16;
    let pointer: Present<Word> = unsafe { std::mem::transmute(&value as *const u16) };
    let raw = <Present<Word> as Address<Word>>::address(pointer);
    assert_eq!(raw, &value as *const u16);
    assert_eq!(unsafe { *raw }, 467);
    assert_eq!(std::mem::size_of::<Option<Present<Word>>>(), std::mem::size_of::<*const u16>());
}
