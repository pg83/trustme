//@ proc-macro-aux-build: proc_macro_with_startup_stdout.rs

#[proc_macro_with_startup_stdout::preserve]
fn value() -> u8 {
    19
}

fn main() {
    assert_eq!(value(), 19);
}
