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

trait Produce<T> {
    fn value() -> impl Copy;
}

impl<T> Produce<T> for () {
    fn value() -> impl Copy {
        17u8
    }
}

fn main() {
    let _ = hidden();
    let value = <() as Produce<<Hidden as Source>::Item>>::value();
    let _copy = value;
    let _again = value;
}
