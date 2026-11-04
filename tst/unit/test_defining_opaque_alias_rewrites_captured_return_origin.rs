//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Hidden = impl Copy;

fn opaque<T: Copy>(value: T) -> impl Copy + use<T> { value }

struct Holder<T>(T);

impl<T> Holder<T> {
    fn size(&self) -> usize { std::mem::size_of::<T>() }
}

#[define_opaque(Hidden)]
fn make_hidden() -> Hidden {
    let value: Hidden = 37u32;
    let holder = Holder(opaque(value));
    assert_eq!(holder.size(), std::mem::size_of::<u32>());
    value
}

fn main() {
    assert_eq!(std::mem::size_of_val(&make_hidden()), 4);
}
