//@ compile-only
//@ compile-flags: -Z stop-after=hir -Z dump-hir
#![feature(type_alias_impl_trait)]

type Thunk = impl FnOnce() -> u8;

#[define_opaque(Thunk)]
const fn make() -> Thunk {
    || 17
}

trait Select {
    type Item;
}

struct At<const N: usize>;

impl<const N: usize> Select for At<N> {
    type Item = Thunk;
}

const WIDTH: usize = std::mem::size_of_val(&std::mem::ManuallyDrop::new(make()));

fn main() {
    let thunk: <At<{ WIDTH }> as Select>::Item = make();
    let _ = thunk();
}
