#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;

#[derive(CoercePointee)]
#[repr(transparent)]
struct View<T: ?Sized>
where
    fn(&T, *const T, [u8; 2], ()) -> *const T: Copy,
    for<'a> &'a [*const T]: Copy,
    (*const T, ()): Copy,
{
    value: *const T,
}

fn main() {
    let values = [17u8, 23, 41];
    let source = View { value: &values as *const [u8; 3] };
    let target: View<[u8]> = source;
    assert_eq!(unsafe { &*target.value }, &[17, 23, 41]);
}
