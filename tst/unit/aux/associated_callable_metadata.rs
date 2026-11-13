#![feature(impl_trait_in_assoc_type)]

pub trait Source {
    type Operation: Fn(u16) -> u16 + Copy;
    fn operation() -> Self::Operation;
}

pub fn add<const BIAS: u16>(value: u16) -> u16 {
    value + BIAS
}

impl Source for () {
    type Operation = impl Fn(u16) -> u16 + Copy;

    fn operation() -> Self::Operation {
        add::<7>
    }
}
