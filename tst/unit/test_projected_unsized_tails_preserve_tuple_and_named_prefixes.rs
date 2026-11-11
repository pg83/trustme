#![feature(unsize)]

use std::marker::{PhantomData, Unsize};

trait Tail {
    type Item: ?Sized;
}

struct Identity<T: ?Sized>(PhantomData<T>);

impl<T: ?Sized> Tail for Identity<T> {
    type Item = T;
}

struct Tuple<H, T: Tail>(H, T::Item);

struct Named<H, T: Tail> {
    header: H,
    payload: T::Item,
}

fn require_unsize<S: ?Sized + Unsize<D>, D: ?Sized>() {}

fn main() {
    require_unsize::<Tuple<u16, Identity<[u8; 3]>>, Tuple<u16, Identity<[u8]>>>();
    require_unsize::<Named<u32, Identity<[u16; 2]>>, Named<u32, Identity<[u16]>>>();

    let tuple = Tuple::<_, Identity<[u8; 3]>>(17u16, [3, 5, 11]);
    let tuple_view: &Tuple<u16, Identity<[u8]>> = &tuple;
    assert_eq!(tuple_view.0, 17);
    assert_eq!(&tuple_view.1, &[3, 5, 11]);

    let named = Named::<_, Identity<[u16; 2]>> {
        header: 29u32,
        payload: [13, 19],
    };
    let named_view: &Named<u32, Identity<[u16]>> = &named;
    assert_eq!(named_view.header, 29);
    assert_eq!(&named_view.payload, &[13, 19]);
}
