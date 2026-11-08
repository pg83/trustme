//@ compile-fail: Coroutine closures take at most one resume argument.
#![feature(coroutines, stmt_expr_attributes)]
fn main() { let _ = #[coroutine] |first: u8, second: u8| { yield first; second }; }
