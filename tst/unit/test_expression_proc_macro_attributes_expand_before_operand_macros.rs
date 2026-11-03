//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

fn main() {
    let generated = #[expression_attribute_controls::generate] 0u32;
    let erased = #[expression_attribute_controls::erase] missing_expression!();
    #[expression_attribute_controls::identity] assert_eq!(generated, 46);
    #[expression_attribute_controls::empty] another_missing_expression!();
    assert_eq!(erased, 17);
}
