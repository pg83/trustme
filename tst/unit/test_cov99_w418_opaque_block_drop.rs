#![feature(type_alias_impl_trait)]

type Hidden = impl Sized;

#[define_opaque(Hidden)]
const VALUE: Hidden = ();

fn main() {
    let value = &{
        let _ = ();
        VALUE
    };
    assert_eq!(std::mem::size_of_val(value), 0);
}
