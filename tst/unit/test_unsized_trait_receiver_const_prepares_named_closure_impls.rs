#![feature(arbitrary_self_types, generic_const_exprs)]
#![allow(incomplete_features)]

trait BorrowAt<const N: usize> {
    type View;
}

impl<T: ?Sized + 'static> BorrowAt<0> for T {
    type View = &'static T;
}

const fn slot() -> usize {
    let _callback = || 31u8;
    0
}

trait Inspect: 'static {
    fn inspect(self: <Self as BorrowAt<{ slot() }>>::View) -> u16;
}

struct Item(u16);

impl Inspect for Item {
    fn inspect(self: &'static Self) -> u16 {
        self.0
    }
}

static ITEM: Item = Item(37);

fn main() {
    assert_eq!(<Item as Inspect>::inspect(&ITEM), 37);
}
