#![feature(auto_traits)]

auto trait Transferable {}

trait Element {
    type Item;
}

struct Source(u8);

impl Element for Source {
    type Item = u16;
}

struct Envelope<T>(T);

impl<T, U> Transferable for Envelope<T>
where
    T: Element<Item = U>,
    U: Copy,
{
}

fn transfer<T: Transferable>(value: T) -> T {
    value
}

fn main() {
    let result = transfer(Envelope(Source(61)));
    assert_eq!(result.0.0, 61);
}
