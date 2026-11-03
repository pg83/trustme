//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-fail: Type mismatch between () and u32
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

fn main() {
    let generated = #[expression_attribute_controls::generate] 0u32;
    let erased = #[expression_attribute_controls::erase] missing_expression!();
    assert_eq!(generated, 46);
    assert_eq!(erased, 17);
    #[expression_attribute_controls::erase] another_missing_expression!();
}
