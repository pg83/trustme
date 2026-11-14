//@ compile-only
//@ compile-flags: -Z stop-after=typeck -Z dump-hir
#![feature(type_alias_impl_trait)]

type Thunk = impl FnOnce() -> u8;

#[define_opaque(Thunk)]
const fn make() -> Thunk {
    || 17
}

trait Identity {
    type Item;
}

impl<T> Identity for T {
    type Item = T;
}

const WIDTH: usize = std::mem::size_of_val(&std::mem::ManuallyDrop::new(make()));

fn main() {
    let thunk: <Thunk as Identity>::Item = make();
    let _ = thunk();
    let _: [u8; WIDTH] = [0; WIDTH];
}
