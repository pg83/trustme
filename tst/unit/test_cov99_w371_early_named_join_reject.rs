//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Default

fn function_item() {}
fn make<T: Default>() -> T {
    loop {}
}

pub struct Item;
struct Selector;
trait Select<const N: usize> {
    type Receiver;
}
impl Select<0> for Selector {
    type Receiver = Box<Item>;
}

impl Item {
    pub fn touch(self: <Selector as Select<{
        let _ = || {
            let _value = if true { function_item } else { make() };
        };
        0
    }>>::Receiver) {}
}
