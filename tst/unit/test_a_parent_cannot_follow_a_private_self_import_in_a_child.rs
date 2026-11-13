//@ compile-fail: error:0:

use bridge::Mirror as Selected;

mod bridge {
    use crate::source::{self as Mirror};
}

pub mod source {
    pub const VALUE: u8 = 17;
}

fn main() {
    let _ = Selected::VALUE;
}
