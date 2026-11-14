//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: error:

use core::marker::PhantomData;

trait Leaf {
    type Item;
}
struct Key<T>(PhantomData<T>);
impl Leaf for Key<u8> {
    type Item = u32;
}

trait Branch {
    type Item;
}
struct Stem<T>(PhantomData<T>);
impl<T> Branch for Stem<T>
where
    Key<T>: Leaf,
{
    type Item = <Key<T> as Leaf>::Item;
}

trait Gate {}
impl<T> Gate for Stem<T>
where
    Stem<T>: Branch<Item = u16>,
{}

fn wants_gate<G: Gate>(_: G) {}

fn main() {
    wants_gate(Stem(PhantomData));
}
