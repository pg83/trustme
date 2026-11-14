//@ compile-fail: Mismatched type param count (expected 0, got 1)
//@ compile-flags: -Cpanic=abort
//@ crate-type: lib

trait MethodArity {
    fn check();
}

struct Subject;

impl MethodArity for Subject {
    fn check<T>() {}
}
