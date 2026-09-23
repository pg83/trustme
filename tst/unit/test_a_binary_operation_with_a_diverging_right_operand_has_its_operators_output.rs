// anyhow's `ensure!(S + break 1 == 1)` with `impl<T> Add<T> for S { type Output
// = bool; }`: upstream types a binary operation as its operator's `Output`
// (`check_overloaded_binop`); a diverging right operand makes the expression
// diverge (`self.diverges`) without changing its type. We typed the whole
// operation `!`, which the fallback turned into `()`, and `()` has no
// `Into<bool>`.
use std::ops::Add;
struct S;
impl<T> Add<T> for S {
    type Output = bool;
    fn add(self, rhs: T) -> bool { let _ = rhs; false }
}
fn not(b: impl Into<bool>) -> bool { !b.into() }
#[allow(unreachable_code)]
fn main() {
    let v = loop {
        if not(S + break 1 == 1) {
            panic!();
        }
    };
    assert!(v);
}
