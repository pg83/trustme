//@ compile-fail: `super` is only allowed at the start of a path
pub const ROOT: u16 = 149;
mod nested {
    pub const LOCAL: u16 = 151;
}

use crate::nested::{super::ROOT as IMPORTED};

fn main() {
    assert_eq!(IMPORTED, 149);
}
