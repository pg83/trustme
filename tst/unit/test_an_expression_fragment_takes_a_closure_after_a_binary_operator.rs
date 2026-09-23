// anyhow's `ensure!(S + || 1 == 1)`: every arm starts with `$cond:expr`, and
// upstream parses a closure wherever an operand may stand (`parse_expr_prefix`
// reaches `parse_expr_closure`), its body taking the rest of the expression:
// `S + (|| (1 == 1))`. The fragment matcher accepted a closure only at the
// start of an expression, so no arm matched.
macro_rules! text {
    ($e:expr) => {
        stringify!($e)
    };
    ($e:expr, $($rest:tt)*) => {
        stringify!($e)
    };
}
fn main() {
    assert_eq!(text!(S + || 1 == 1), "S + || 1 == 1");
    assert_eq!(text!(S + move || 1 == 1), "S + move || 1 == 1");
    assert_eq!(text!(S + |()| 1 == 1, x), "S + |()| 1 == 1");
    assert_eq!(text!(S * move |a, b| a + b), "S * move |a, b| a + b");
}
