#![feature(derive_coerce_pointee)]
use std::marker::{CoercePointee, PhantomData};

#[derive(CoercePointee)]
#[repr(transparent)]
struct View<const N: usize = 3, #[pointee] T: ?Sized = u16> {
    value: *const T,
    marker: PhantomData<[(); N]>,
}

fn main() {
    let values = [17u16, 259];
    let source: View<3, [u16; 2]> = View { value: &values, marker: PhantomData };
    let target: View<3, [u16]> = source;
    assert_eq!(unsafe { &*target.value }, &[17, 259]);
}
