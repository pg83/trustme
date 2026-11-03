//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! import_identity {
    () => { use expression_attribute_controls::identity as pending_identity; };
}

fn main() {
    let checked = #[pending_identity] {
        #[allow(non_camel_case_types)]
        type bool = u32;
        #[derive(PartialEq)]
        struct Value(u8);
        let left = Value(19);
        left == Value(19)
    };
    import_identity!();
    assert!(checked);
}
