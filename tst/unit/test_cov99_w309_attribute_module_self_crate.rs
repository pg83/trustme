//@ proc-macro-aux-build: cov99_w309_self_crate_attr.rs

mod numbers {
    pub const VALUE: u8 = 21;
}

#[cov99_w309_self_crate_attr::keep]
mod imports {
    extern crate self as owner;
    use owner::numbers::{self as selected, VALUE};

    pub fn sum() -> u8 {
        selected::VALUE + VALUE
    }
}

fn main() {
    assert_eq!(imports::sum(), 42);
}
