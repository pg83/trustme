#![feature(trait_alias)]

trait CopyCells = Iterator<Item: Copy>;

fn echo<T: Copy>(value: T) -> T { value }

fn main() {
    let first = echo::<<dyn CopyCells<Item = _> as Iterator>::Item>(31u16);
    let second = <<dyn CopyCells<Item = _> as Iterator>::Item as Clone>::clone(&37u32);
    assert_eq!((first, second), (31u16, 37u32));
}
