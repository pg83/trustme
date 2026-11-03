//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

fn main() {
    let first = #[expression_attribute_controls::twice]
                #[expression_attribute_controls::increment] 20u32;
    let second = #[expression_attribute_controls::increment]
                 #[expression_attribute_controls::twice] 20u32;
    let third = #[expression_attribute_controls::identity]
                #[expression_attribute_controls::increment] 20u32;
    assert_eq!(first, 42);
    assert_eq!(second, 41);
    assert_eq!(third, 21);
}
