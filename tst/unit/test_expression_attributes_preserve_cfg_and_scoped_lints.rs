//@ compile-variant: -O
#![feature(stmt_expr_attributes)]

fn main() {
    #[cfg(any())] #[cfg(all())] missing_disabled_macro!();
    #[cfg(any())] { panic!("disabled block ran"); }
    let value = #[cfg(all())] #[allow(unused_variables)] {
        let unused = 59u32;
        61u32
    };
    assert_eq!(value, 61);
}
