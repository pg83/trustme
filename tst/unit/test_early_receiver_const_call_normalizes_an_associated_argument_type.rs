//@ compile-fail: destructor
#![feature(arbitrary_self_types, generic_const_exprs)]
#![allow(incomplete_features)]

trait Carrier {
    type Value;
}

impl Carrier for u8 {
    type Value = usize;
}

const fn slot<T: Carrier>(_: T::Value) -> usize {
    let _callback = || 53u8;
    0
}

trait Share<const N: usize> {
    type Ref;
}

impl<T: ?Sized + 'static> Share<0> for T {
    type Ref = &'static T;
}

trait Read: 'static {
    fn read(self: <Self as Share<{ slot::<u8>(59) }>>::Ref) -> u8;
}

struct Item(u8);
static ITEM: Item = Item(61);

impl Read for Item {
    fn read(self: &'static Self) -> u8 {
        self.0
    }
}

fn main() {
    assert_eq!(<Item as Read>::read(&ITEM), 61);
}
