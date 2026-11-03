//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-fail: macro expansion ignores
#![feature(proc_macro_hygiene, stmt_expr_attributes)]
fn main() {
    let _value = #[expression_attribute_controls::trailing] 0u32;
}
