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

impl<T> Stem<T>
where
    Stem<T>: Branch<Item = u16>,
{
    fn wants_u16(&self) {}
}

fn main() {
    Stem::<u8>(PhantomData).wants_u16();
}
