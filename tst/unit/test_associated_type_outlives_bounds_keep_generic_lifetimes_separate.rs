use std::any::TypeId;

trait Shorten<'long, 'short>
where
    'long: 'short,
{
    type View: 'short;
    fn shorten(value: &'long u16) -> Self::View;
}

struct Keeper;

impl<'long: 'short, 'short> Shorten<'long, 'short> for Keeper {
    type View = &'short u16;
    fn shorten(value: &'long u16) -> Self::View { value }
}

fn main() {
    let value = 97;
    let borrowed = <Keeper as Shorten<'_, '_>>::shorten(&value);
    assert_eq!(*borrowed, 97);
    assert!(std::ptr::eq(borrowed, &value));
    assert_ne!(TypeId::of::<fn(&'static u16)>(),
               TypeId::of::<for<'a> fn(&'a u16)>());
}
