//@ compile-fail: cannot be evaluated at compile-time
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

const CHECK: () = {
    let _value = #[coroutine] || { yield 89u8; };
};

fn main() { let _ = CHECK; }
