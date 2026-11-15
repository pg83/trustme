//@ aux-build: cov99_w1002_legacy_const.rs
//@ compile-flags: -O

extern crate cov99_w1002_legacy_const;

#[inline(never)]
fn select<const N: usize, const M: usize>(first: bool) -> usize {
    if first {
        cov99_w1002_legacy_const::value(N)
    } else {
        cov99_w1002_legacy_const::value(M)
    }
}

fn main() {
    assert_eq!(select::<3, 5>(true), 3);
    assert_eq!(select::<3, 5>(false), 5);
}
