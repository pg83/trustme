//@ compile-fail: `super` is only allowed at the start of a path
pub const ROOT: u16 = 157;
mod nested {
    pub const LOCAL: u16 = 159;
}

use self::nested::{super::ROOT as IMPORTED};

fn main() {
    assert_eq!(IMPORTED, 157);
}
