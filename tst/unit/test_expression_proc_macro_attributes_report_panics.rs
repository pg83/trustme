//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-fail: custom attribute panicked
#![feature(proc_macro_hygiene, stmt_expr_attributes)]
fn main() {
    let _value = #[expression_attribute_controls::broken] 0u32;
}
