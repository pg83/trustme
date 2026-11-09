//@ compile-fail: error
#![feature(stmt_expr_attributes)]

fn main() {
    let _value = !#[cfg(any())] true;
}
