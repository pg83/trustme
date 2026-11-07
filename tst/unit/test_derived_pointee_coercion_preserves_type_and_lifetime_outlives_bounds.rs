#![feature(derive_coerce_pointee)]

use std::marker::{CoercePointee, PhantomData};

#[derive(CoercePointee)]
#[repr(transparent)]
struct View<'short, 'long: 'short, #[pointee] T: ?Sized + 'long>
where
    T: 'short,
{
    value: &'long T,
    scope: PhantomData<&'short ()>,
}

fn widen<'short, 'long: 'short>(
    value: &'long [u16; 3],
    _: &'short (),
) -> View<'short, 'long, [u16]> {
    let thin = View { value, scope: PhantomData };
    thin
}

fn main() {
    let values = [61u16, 67, 71];
    let scope = ();
    let wide = widen(&values, &scope);
    assert_eq!(wide.value, &[61, 67, 71]);
    assert_eq!(wide.value.len(), 3);
    assert_eq!(wide.value.as_ptr(), values.as_ptr());
    assert_eq!(std::mem::size_of_val(&wide), std::mem::size_of::<&[u16]>());
}
