use std::marker::PhantomData;

trait Tail {
    type Type<'a>: ?Sized
    where
        Self: 'a;
}

struct Wrapper<'a, T: Tail + 'a>(T::Type<'a>);

struct Identity<T: ?Sized>(PhantomData<T>);

impl<T: ?Sized> Tail for Identity<T> {
    type Type<'a> = T where Self: 'a;
}

fn coerce<'a>(value: &'a Wrapper<'a, Identity<[u8; 3]>>) -> &'a Wrapper<'a, Identity<[u8]>> {
    value
}

fn main() {
    let value = Wrapper::<'_, Identity<[u8; 3]>>([1, 2, 3]);
    assert_eq!(coerce(&value).0.len(), 3);
}
