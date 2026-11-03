//@ proc-macro-aux-build: expression_attribute_controls.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! duplicate {
    ($body:expr) => { ($body, #[expression_attribute_controls::identity] $body) };
}

fn main() {
    let pair = duplicate!({
        fn selected() -> u32 {
            fn nested() -> u32 { 43 }
            nested()
        }
        selected()
    });
    assert_eq!(pair, (43, 43));
}
