#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! __rust_unstable_column {
    () => {};
}

fn main() {
    let columns = (
        __rust_unstable_column!(),
            __rust_unstable_column!(),
    );
    assert_eq!(columns.0, 9u32);
    assert_eq!(columns.1, 13u32);
}
