mod source {
    pub struct Number(pub u16);
    pub fn read(value: Number) -> u16 { value.0 }
}
mod narrow { pub(super) use crate::source::{Number, read}; }
mod wide { pub use crate::source::{Number, read}; }
mod bridge {
    use crate::narrow::*;
    pub use crate::wide::*;
}
mod consumer {
    pub fn value() -> u16 { crate::bridge::read(crate::bridge::Number(259)) }
}

fn main() { assert_eq!(consumer::value(), 259); }
