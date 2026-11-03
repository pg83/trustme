#![feature(coerce_unsized, unsize)]
use std::marker::{PhantomData, Unsize};
use std::ops::CoerceUnsized;
struct Tuple<T: ?Sized>(*const T, PhantomData<T>);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Tuple<U>> for Tuple<T> {}
struct Named<T: ?Sized> { marker: PhantomData<T>, pointer: *const T, tag: u8 }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Named<U>> for Named<T> {}
fn main() {
    let values = [157u16, 163, 167];
    let tuple = Tuple(&values as *const [u16; 3], PhantomData);
    let slice: Tuple<[u16]> = tuple;
    assert_eq!(unsafe { &*slice.0 }, &[157, 163, 167]);
    let named = Named { marker: PhantomData, pointer: &values as *const [u16; 3], tag: 17 };
    let named_slice: Named<[u16]> = named;
    assert_eq!(named_slice.tag, 17);
    assert_eq!(unsafe { &*named_slice.pointer }.len(), 3);
    assert_eq!(unsafe { (*named_slice.pointer)[1] }, 163);
}
