//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Hidden = impl Copy;

struct Source<T>(PhantomData<T>);

impl<T: Copy> Source<T> {
    fn opaque(value: T) -> impl Copy + use<T> { value }
}

struct Holder<T>(T);

impl<T> Holder<T> {
    fn size(&self) -> usize { std::mem::size_of::<T>() }
}

#[define_opaque(Hidden)]
fn make_hidden() -> Hidden {
    let value: Hidden = 41u64;
    let holder = Holder(Source::<Hidden>::opaque(value));
    assert_eq!(holder.size(), std::mem::size_of::<u64>());
    value
}

fn main() {
    assert_eq!(std::mem::size_of_val(&make_hidden()), 8);
}
