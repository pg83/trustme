//@ compile-flags: -Znext-solver=globally

// Heads are selected by the self CONSTRUCTOR: a concrete constructor with
// zero head matches stays NoSolution no matter what its arguments resolve
// to.  `[?i; 3]: ExactSizeIterator` has no impl with an array self; keeping
// it ambiguous lets the `&mut I` blanket shadow the inherent slice `len`.
// Mirrors core/ptr/non_null doctests.

fn main() {
    let x = &mut [1, 2, 4];
    assert_eq!(x.len(), 3);
    let n: u64 = x.len() as u64;
    assert_eq!(n, 3);
}
