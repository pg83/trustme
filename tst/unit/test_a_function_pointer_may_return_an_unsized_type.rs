// zerovec's serde visitor carries `PhantomData<(fn() -> T, F)>` with `T = str`.
// A function pointer type needs no sized return type until it is called, and
// one returning `str` can never be; codegen emitted its C signature and gave up
// on the `str`.
use std::marker::PhantomData;

struct Visitor<T: ?Sized> {
    marker: PhantomData<(fn() -> T, u8)>,
}

fn main() {
    let visitor: Visitor<str> = Visitor { marker: PhantomData };
    let marker = visitor.marker;
    let _ = marker;
    let f: Option<fn() -> str> = None;
    assert!(f.is_none());
    let g: Option<fn() -> [u8]> = None;
    assert!(g.is_none());
    assert_eq!(std::mem::size_of::<Option<fn() -> str>>(), std::mem::size_of::<usize>());
}
