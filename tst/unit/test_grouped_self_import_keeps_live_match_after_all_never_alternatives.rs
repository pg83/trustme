#![feature(never_patterns)]

mod source {
    pub enum Empty {}
    pub const VALUE: Option<Empty> = None;
}

use source::{self as values, VALUE};

fn main() {
    let value: Option<values::Empty> = VALUE;
    let unit = match value {
        Some(!) | Some(!),
        None => {
            #![allow(unused_variables)]
        }
    };
    assert_eq!(unit, ());
}
