#![feature(arbitrary_self_types)]

trait Storage { type Owner; }
struct Item(u16);
impl Storage for Item { type Owner = Box<Item>; }

impl Item {
    fn read(self: <Self as Storage>::Owner) -> u16 { self.0 }
}

fn main() {
    assert_eq!(Box::new(Item(43)).read(), 43);
}
