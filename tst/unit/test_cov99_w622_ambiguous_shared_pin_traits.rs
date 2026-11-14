//@ compile-fail: multiple applicable items in scope
#![feature(pin_ergonomics)]

use std::pin::Pin;

struct Item;

trait First {
    fn inspect(self: Pin<&Self>);
}

trait Second {
    fn inspect(self: Pin<&Self>);
}

impl First for Item {
    fn inspect(self: Pin<&Self>) {}
}

impl Second for Item {
    fn inspect(self: Pin<&Self>) {}
}

fn main() {
    let mut item = Item;
    let receiver = Pin::new(&mut item);
    receiver.inspect();
}
