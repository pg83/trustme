//@ edition: 2015

// The typechecker equates `!` with any type - an expression of type `!` never
// produces a value, so nothing it is compared against can disagree with it.
// That equality leaves the node holding the type it diverged with, so `slot =
// &panic!()` recorded `&!` against a `&usize` slot, and the later passes, which
// read the node's type back and compare it strictly, aborted the compiler.

fn main() {
    let value = 5usize;
    let mut slot: &usize = &value;
    if std::env::args().count() > 99 {
        slot = &panic!();
    }
    assert_eq!(*slot, 5);
}
