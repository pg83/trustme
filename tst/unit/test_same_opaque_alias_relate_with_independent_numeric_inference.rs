//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Hidden = impl Copy;
struct Wrap<T>(T);

trait Relation<R, U> {}
impl<T, U> Relation<Wrap<T>, U> for Wrap<T> {}

fn relate<X, Y, U>(_: &Wrap<X>, _: &Wrap<Y>, tag: U) -> U
where Wrap<X>: Relation<Wrap<Y>, U>
{
    tag
}

#[define_opaque(Hidden)]
fn make() -> Hidden { 29u16 }

fn main() {
    let first = Wrap(make());
    let second = Wrap(make());
    let tag = relate::<Hidden, Hidden, _>(&first, &second, 11);
    assert_eq!(tag, 11);
    assert_eq!(std::mem::size_of_val(&first.0), 2);
    assert_eq!(std::mem::size_of_val(&second.0), 2);
}
