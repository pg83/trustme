#![feature(coroutines, stmt_expr_attributes)]

fn main() {
    let _coroutine = #[coroutine] static || {
        let mut text = String::new();
        text += { yield; "" };
    };
}
