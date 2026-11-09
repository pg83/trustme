//@ compile-fail: error
#![feature(rustc_attrs, cfg_select)]

#[rustc_builtin_macro]
macro_rules! cfg_select {
    ($($tokens:tt)*) => {};
}

fn main() {
    let _value = cfg_select! { any() => 17u8 };
}
