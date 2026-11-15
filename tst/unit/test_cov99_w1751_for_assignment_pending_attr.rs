//@ edition: 2021
//@ compile-fail: invalid left-hand side of assignment
fn main() {
    (#[cov99_w1751_pending] for _ in 0..0 {}) = ();
}
