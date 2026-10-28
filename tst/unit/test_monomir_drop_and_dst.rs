//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zdump-hir -Zdump-mir -Zmir-opt-level=0

pub struct Owned(pub String);

impl Drop for Owned {
    fn drop(&mut self) { self.0.clear(); }
}

pub trait Read {
    fn read(&self) -> usize;
}

impl Read for Owned {
    fn read(&self) -> usize { self.0.len() }
}

pub struct Tail<T: ?Sized> { pub prefix: u8, pub tail: T }
pub struct TupleTail<T: ?Sized>(pub u8, pub T);

pub fn metadata(slice: &Tail<[u16]>, text: &TupleTail<str>, object: &Tail<dyn Read>) -> usize {
    slice.tail.len() + text.1.len() + object.tail.read()
}

pub fn unsize<'a>(value: &'a Tail<[u16; 3]>, owned: &'a Owned) -> (&'a Tail<[u16]>, &'a dyn Read) {
    (value, owned)
}

pub fn partial_move(value: (Owned, Owned), take_first: bool) -> usize {
    let selected = if take_first { value.0 } else { value.1 };
    selected.read()
}

pub fn conditional_drop(flag: bool, text: String) -> usize {
    let owned;
    if flag {
        owned = Owned(text);
    } else {
        return text.len();
    }
    owned.read()
}

pub fn boxed(text: String) -> Box<dyn Read> {
    Box::new(Owned(text))
}
