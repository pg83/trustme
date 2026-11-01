//@ compile-fail: `become` is not allowed in closures
#![feature(explicit_tail_calls, coroutines, stmt_expr_attributes)]
fn target(value: u16) -> u16 { value + 1 }
fn main() {
    let _ = #[coroutine] || {
        yield 7u16;
        become target(11)
    };
}
