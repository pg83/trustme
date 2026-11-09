#![feature(auto_traits)]

auto trait Transferable {}

trait Element {
    type Item;
}

trait Flavor {
    type Output;
}

impl Flavor for [u8; 1] {
    type Output = u16;
}

struct Source(u8);

impl Element for Source {
    type Item = u8;
}

struct Envelope<T>(T);

impl<T, U> Transferable for Envelope<T>
where
    [U; 1]: Flavor<Output = u16>,
    T: Element<Item = U>,
{
}

fn transfer<T: Transferable>(value: T) -> T {
    value
}

fn main() {
    let result = transfer(Envelope(Source(83)));
    assert_eq!(result.0.0, 83);
}
