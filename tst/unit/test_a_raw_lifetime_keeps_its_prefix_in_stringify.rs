//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// quote's `quote!('r#async)` spells the lifetime with
// `stringify!($lifetime)`; the lifetime lost its `r#` and printed as
// `'async`. rustc keeps the rawness on the token
// (`Token::Lifetime(name, IdentIsRaw::Yes)`), prints it as `'r#name`, and
// hands it to a proc macro as `'` followed by a raw identifier; the lifetime
// the parser makes of it is the plain name.
use proc_macro_item_passthrough::{echo, token_texts};

macro_rules! spell {
    ($l:lifetime) => {
        stringify!($l)
    };
}

fn same<'r#a>(x: &'a u8) -> &'r#a u8 {
    x
}

fn main() {
    assert_eq!(stringify!('r#async), "'r#async");
    assert_eq!(spell!('r#async), "'r#async");
    assert_eq!(spell!('r#a), "'r#a");
    assert_eq!(spell!('a), "'a");
    assert_eq!(stringify!(&'r#fn u8), "&'r#fn u8");
    assert_eq!(token_texts!('r#async), "' r#async");
    assert_eq!(echo!(stringify!('r#async)), "'r#async");
    assert_eq!(*same(&3), 3);
}
