//@ compile-variant: -O
use std::marker::PhantomData;
struct Owner<T>(PhantomData<T>);
struct Wrap<T>(Option<T>);
enum Choice<T> { Absent, Present(T) }

impl<T: 'static> Owner<T> {
    const TUPLE: &'static (Option<T>, u16) = &(None, 17);
    const WRAPPED: &'static Wrap<T> = &Wrap(None);
    const ENUM: &'static Choice<T> = &Choice::Absent;
}

fn main() {
    assert_eq!(Owner::<String>::TUPLE.1, 17);
    assert!(Owner::<String>::TUPLE.0.is_none());
    assert!(Owner::<String>::WRAPPED.0.is_none());
    assert!(matches!(Owner::<String>::ENUM, Choice::Absent));
    assert!(matches!(Choice::Present(23u8), Choice::Present(23)));
}
