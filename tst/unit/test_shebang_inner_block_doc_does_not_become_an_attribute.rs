#!/*! documentation */ [ this is discarded
//@ compile-variant: -O

const STEP: usize = 11;
fn triangular(n: usize) -> usize { (1..=n).sum() }
fn main() {
    assert_eq!(triangular(STEP), 66);
    assert_eq!(triangular(3), 6);
}
