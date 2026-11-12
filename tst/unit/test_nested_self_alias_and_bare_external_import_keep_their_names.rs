use ::core;

mod source {
    pub mod inner {
        pub const VALUE: u16 = 1021;
    }
}

use source::{inner::self as selected};

fn main() {
    assert_eq!(selected::VALUE, 1021);
    assert_eq!(core::mem::size_of::<u16>(), 2);
}
