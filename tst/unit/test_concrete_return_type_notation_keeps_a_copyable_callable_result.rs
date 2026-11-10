#![feature(return_type_notation)]
//@ compile-fail: ambiguous associated function
trait Maker {
    fn make() -> impl Fn() -> u16 + Copy;
}

struct Factory;

fn produce() -> u16 { 47 }

impl Maker for Factory {
    fn make() -> impl Fn() -> u16 + Copy { produce }
}

fn observe()
where
    Factory::make(..): Copy,
{
    let first = Factory::make();
    let second = first;
    assert_eq!(first() + second(), 94);
}

fn main() { observe(); }
