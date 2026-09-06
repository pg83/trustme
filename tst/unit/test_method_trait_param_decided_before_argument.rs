//@ run-pass
// `self.0.borrow().eq(&other.0.borrow())`: `eq` is found on `BTreeSet` under the
// `Ref`, with `PartialEq<?Rhs>` still open.  Upstream registers that obligation
// at lookup and selects it before coercing an argument to a parameter that still
// holds an inference variable (`resolve_vars_with_obligations`): the only impl
// decides `Rhs = BTreeSet<u32>`, and `&Ref<BTreeSet<u32>>` then reaches
// `&BTreeSet<u32>` by dereferencing.  Related to the argument first, `Rhs` would
// have been `Ref<BTreeSet<u32>>`, which no impl provides.
use std::cell::RefCell;
use std::collections::BTreeSet;

struct Group(RefCell<BTreeSet<u32>>);

impl PartialEq for Group {
    fn eq(&self, other: &Group) -> bool {
        self.0.borrow().eq(&other.0.borrow())
    }
}

fn main() {
    let a = Group(RefCell::new(BTreeSet::from([1, 2])));
    let b = Group(RefCell::new(BTreeSet::from([2, 1])));
    let c = Group(RefCell::new(BTreeSet::from([3])));
    assert!(a == b);
    assert!(!(a == c));
}
