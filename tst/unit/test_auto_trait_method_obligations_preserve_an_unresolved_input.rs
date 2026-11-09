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

struct Source<T>(T);

impl<T> Element for Source<T> {
    type Item = T;
}

struct Envelope<T>(T);

impl<T, U> Transferable for Envelope<T>
where
    [U; 1]: Flavor<Output = u16>,
    T: Element<Item = U>,
{
}

struct Gate;

impl Gate {
    fn transfer<T: Transferable>(&self, value: T) -> T {
        value
    }
}

fn main() {
    let input = Default::default();
    let result = Gate.transfer(Envelope(Source(input)));
    let byte: u8 = result.0.0;
    assert_eq!(byte, 0);
}
