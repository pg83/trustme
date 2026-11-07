//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![feature(return_type_notation)]

use proc_macro_item_passthrough::token_texts;

trait Factory {
    fn make() -> impl Copy;
}

macro_rules! spell {
    ($pattern:pat) => {
        token_texts!($pattern)
    };
}

fn main() {
    let spelling = spell!(Wrapper::<dyn Factory<make(..): Copy>>(_));
    let compact: String = spelling.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "Wrapper::<dynFactory<make(..):Copy>>(_)");
}
