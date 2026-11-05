#![feature(decl_macro, rustc_attrs)]
#![allow(internal_features)]

const ALL: bool = later::cfg!(all());
const ANY: bool = later::cfg!(any());

mod later {
    #[rustc_builtin_macro]
    pub macro cfg($($tokens:tt)*) {}
}

fn main() {
    assert!(ALL);
    assert!(!ANY);
}
