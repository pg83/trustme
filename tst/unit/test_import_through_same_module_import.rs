mod outer {
    pub mod imported {
        pub mod imported {
            pub fn value() {}
        }
    }
}

use outer::*;
use imported::imported;
use imported::value;

fn main() {
    value();
}
