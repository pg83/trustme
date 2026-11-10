#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! __register_diagnostic {
    () => {};
}

#[rustc_builtin_macro]
macro_rules! __diagnostic_used {
    () => {};
}

mod local {
    const BEFORE: u16 = 19;

    __register_diagnostic!(E7132, "local marker");
    __diagnostic_used!(E7132);

    pub fn value() -> u16 {
        BEFORE + 8
    }
}

fn main() {
    assert_eq!(local::value(), 27);
}
