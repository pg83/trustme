//@ compile-fail: error

trait Source {
    type Item;
    fn read(&self) -> Self::Item;
}

struct Counter;

impl Source for Counter {
    type Item = u16;
    fn read(&self) -> u16 { 137 }
}

fn main() {
    let _: bool = Counter.read();
}
