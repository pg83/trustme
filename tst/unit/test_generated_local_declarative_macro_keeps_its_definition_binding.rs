//@ proc-macro-aux-build: own_generated_local_macro_context.rs
#![feature(decl_macro)]

own_generated_local_macro_context::local_context!();

fn main() {
    let local = 1000u16;
    assert_eq!(generated_value(), 42);
    assert_eq!(local, 1000);
}
