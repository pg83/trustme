#![feature(arbitrary_self_types, generic_const_exprs)]
#![allow(incomplete_features)]

trait Select<T: ?Sized, const N: usize> {
    type Ref;
}

struct Selector;

impl<T: ?Sized + 'static> Select<T, 0> for Selector {
    type Ref = &'static T;
}

const fn selected_slot() -> usize {
    let _callback = || 41u8;
    0
}

trait Read: 'static {
    fn read(self: <Selector as Select<Self, { selected_slot() }>>::Ref) -> u8;
}

struct Item(u8);

impl Read for Item {
    fn read(self: &'static Self) -> u8 {
        self.0
    }
}

static ITEM: Item = Item(43);

fn main() {
    assert_eq!(<Item as Read>::read(&ITEM), 43);
}
