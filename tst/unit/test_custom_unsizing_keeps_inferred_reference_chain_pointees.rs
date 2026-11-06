#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
struct View<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<View<U>> for View<T> {}
fn echo<T: ?Sized>(view: View<T>) -> View<T> { view }
fn main() {
    let data = [5u16, 8, 13];
    let a = &data;
    let b = &a;
    let inferred: View<_> = echo(View(&b));
    assert_eq!(unsafe { (***inferred.0)[2] }, 13);
    let direct: View<_> = View(&data);
    assert_eq!(unsafe { (*direct.0)[1] }, 8);
    let dynamic: View<[u16]> = View(&data);
    assert_eq!(unsafe { (&*dynamic.0).len() }, 3);
}
