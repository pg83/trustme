//@ run-pass
// `v.append(&mut w)` on a `VecDeque<_>` whose element type is still open, with
// `w` an unknown `?W` from `vec![()].into()`.  Upstream's method probe keeps
// an inherent impl unless one of its where-clauses cannot hold
// (`predicate_may_hold`); `_: Sized` still open is registered at confirmation
// as an obligation, the method is picked, and relating the argument gives
// `?W = VecDeque<_>`, from which `?W: From<Vec<()>>` decides the element.
// Holding the pick back until the bounds were certain left both unknown
// (alloctests vec_deque test_append_zst_capacity_overflow).
use std::collections::VecDeque;

fn main() {
    let mut v = Vec::with_capacity(8);
    unsafe { v.set_len(8) };
    let mut v = VecDeque::from(v);
    let mut w = vec![()].into();
    v.append(&mut w);
    assert_eq!(v.len(), 9);
    assert_eq!(w.len(), 0);
}
