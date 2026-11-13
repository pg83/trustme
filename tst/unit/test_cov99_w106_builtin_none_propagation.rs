#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
#[macro_export]
macro_rules! stringify {
    ($($token:tt)*) => {};
}

#[macro_use]
mod inherit_builtin {}

fn main() {
    assert_eq!(stringify!(inherited_builtin), "inherited_builtin");
}
