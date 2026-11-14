//@ compile-fail: Mismatched const param count (expected 1, got 0)
//@ compile-flags: --emit=metadata -Cpanic=abort

trait ConstArity {
    fn check<const N: usize>();
}

struct Subject;

impl ConstArity for Subject {
    fn check() {}
}

fn main() {}
