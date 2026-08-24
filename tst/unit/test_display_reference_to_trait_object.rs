//@ compile-flags: -O
//@ crate-type: lib

use std::fmt::{self, Display, Formatter};

pub trait Expected {
    fn fmt(&self, formatter: &mut Formatter) -> fmt::Result;
}

impl Display for dyn Expected + '_ {
    fn fmt(&self, formatter: &mut Formatter) -> fmt::Result {
        Expected::fmt(self, formatter)
    }
}

pub fn format_reference(index: usize, value: &dyn Expected) -> String {
    format!("invalid length {}, expected {}", index, value)
}
