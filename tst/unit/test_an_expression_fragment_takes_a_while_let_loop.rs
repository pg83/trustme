// anyhow's `ensure!(while let None = Some(1) {}.t(1) == 2)` reaches
// `__fancy_ensure!($lhs:expr, ..)` with a `while let` loop as the start of an
// expression. Upstream parses the condition of `while`, as of `if`, with
// `parse_expr_cond`, which takes `let PAT = EXPR`; the fragment matcher took
// `let` only after `if`, so no arm matched.
macro_rules! text {
    ($e:expr, $op:tt, $r:expr) => {
        stringify!($e)
    };
}
trait T {
    fn t(&self, v: i32) -> i32 {
        v
    }
}
impl T for () {}
fn main() {
    assert_eq!(text!(while let None = Some(1) {}.t(1), ==, 2), "while let None = Some(1) {}.t(1)");
    let mut n = 0;
    while let Some(v) = if n < 3 { Some(n) } else { None } {
        n = v + 1;
    }
    assert_eq!(n, 3);
}
