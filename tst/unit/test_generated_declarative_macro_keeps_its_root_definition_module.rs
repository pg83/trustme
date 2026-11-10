//@ proc-macro-aux-build: own_generated_root_macro_context.rs
#![feature(decl_macro)]

own_generated_root_macro_context::root_context!();

fn helper() -> u16 { 1000 }

fn main() {
    assert_eq!(generated::read!(5), 42);
    assert_eq!(helper(), 1000);
}
