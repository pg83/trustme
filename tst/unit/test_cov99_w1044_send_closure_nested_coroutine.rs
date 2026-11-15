//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(coroutines, stmt_expr_attributes)]

fn require_send<T: Send>(_: T) {}

fn main() {
    let seed = 7u8;
    require_send(move || {
        let _step = #[coroutine] move || {
            yield seed;
        };
    });
}
