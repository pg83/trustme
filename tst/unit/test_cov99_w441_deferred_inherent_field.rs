#![feature(type_alias_impl_trait, inherent_associated_types)]
#![allow(incomplete_features)]

struct Carrier<T>(core::marker::PhantomData<T>);
impl<T> Carrier<T> {
    type Field = u8;
}

type Hidden = impl Copy;
#[define_opaque(Hidden)]
fn define_hidden() -> Hidden {
    0u8
}

struct Pending(Carrier<Hidden>::Field);
const VALUE: Pending = Pending(17);
const A_READ: usize = {
    let _value = VALUE;
    1
};

fn main() {
    assert_eq!(A_READ, 1);
    let _ = define_hidden();
}
