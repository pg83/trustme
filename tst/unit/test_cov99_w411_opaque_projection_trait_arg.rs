#![feature(type_alias_impl_trait)]

trait Source {
    type Item;
}

struct Concrete;

impl Source for Concrete {
    type Item = u8;
}

type Hidden = impl Source;

#[define_opaque(Hidden)]
fn hidden() -> Hidden {
    Concrete
}

trait Value<T> {
    fn value() -> u8;
}

impl<T> Value<T> for () {
    fn value() -> u8 {
        17
    }
}

fn main() {
    let _ = hidden();
    assert_eq!(<() as Value<<Hidden as Source>::Item>>::value(), 17);
}
