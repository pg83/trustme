//@ compile-fail: type annotations needed
#![feature(type_alias_impl_trait)]

type First = impl Copy;
type Second = impl Copy;
struct Wrap<T>(T);

trait Relation<R, U> {}
impl<T, U> Relation<Wrap<T>, U> for Wrap<T> {}

fn relate<X, Y, U>(_: &Wrap<X>, _: &Wrap<Y>, tag: U) -> U
where Wrap<X>: Relation<Wrap<Y>, U>
{
    tag
}

#[define_opaque(First, Second)]
fn a_combine() -> (First, Second) {
    let first = z_first();
    let second = z_second();
    let tag = relate::<First, Second, _>(&Wrap(first), &Wrap(second), 7);
    assert_eq!(tag, 7);
    (first, second)
}

#[define_opaque(First)]
fn z_first() -> First { 19u8 }

#[define_opaque(Second)]
fn z_second() -> Second { 23u8 }

fn main() {
    let pair = a_combine();
    assert_eq!(std::mem::size_of_val(&pair.0), 1);
    assert_eq!(std::mem::size_of_val(&pair.1), 1);
}
