#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Select: Sized {
    type Item;

    fn choose(_: Self, item: <Self as Select>::Item) -> <Self as Select>::Item {
        item
    }
}

impl Select for u8 {
    type Item = u16;
}

reuse Select::choose as delegated;

fn main() {
    assert_eq!(delegated(7u8, 513u16), 513u16);
}
