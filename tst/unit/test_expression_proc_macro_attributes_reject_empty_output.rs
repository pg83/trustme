//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-fail: expected expression
#![feature(proc_macro_hygiene, stmt_expr_attributes)]
fn main() {
    let _value = #[expression_attribute_controls::empty] 0u32;
}
