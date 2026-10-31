#![feature(derive_coerce_pointee)]
use std::marker::{CoercePointee, PhantomData};

trait Tag<T: ?Sized> {}
trait Identity { type Item: Copy; }
impl<T: Copy> Identity for T { type Item = T; }

#[derive(CoercePointee)]
#[repr(transparent)]
struct View<U: Copy, #[pointee] T: ?Sized>
where
    for<'a> &'a dyn Tag<T>: Copy,
    fn(U) -> U: Copy,
    fn(*const T): Copy,
    fn() -> !: Copy,
    <*const T as Identity>::Item: Copy,
{
    value: *const T,
    tag: PhantomData<U>,
}

fn main() {
    let values = [19u16, 37];
    let source = View { value: &values as *const [u16; 2], tag: PhantomData::<u8> };
    let target: View<u8, [u16]> = source;
    assert_eq!(unsafe { &*target.value }, &[19, 37]);
}
