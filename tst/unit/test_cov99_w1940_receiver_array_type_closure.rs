#![feature(arbitrary_self_types)]

struct Item;

trait View<T> {
    type Ref;
}

impl<T> View<T> for Item {
    type Ref = &'static Item;
}

impl Item {
    fn read(self: <Item as View<[u8; { let _callback = || 79u8; 0 }]>>::Ref) -> u8 {
        83
    }
}

static ITEM: Item = Item;

fn main() {
    assert_eq!((&ITEM).read(), 83);
}
