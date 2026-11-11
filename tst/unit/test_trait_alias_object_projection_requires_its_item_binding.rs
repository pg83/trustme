//@ compile-fail: associated type `Item` must be specified
#![feature(trait_alias)]

trait CopyCells = Iterator<Item: Copy>;

fn main() {
    let _ = <<dyn CopyCells as Iterator>::Item as Clone>::clone(&41u16);
}
