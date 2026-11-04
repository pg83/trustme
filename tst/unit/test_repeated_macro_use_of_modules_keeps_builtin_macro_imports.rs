//@ compile-variant: -O

#[macro_use]
#[macro_use]
mod builtin_macros {
    pub use core::{cfg, concat, stringify};
}

fn main() {
    assert!(builtin_macros::cfg!(all()));
    assert_eq!(builtin_macros::concat!("fir", "tree"), "firtree");
    assert_eq!(builtin_macros::stringify!(green + leaf), "green + leaf");
    assert!(cfg!(all()));
}
