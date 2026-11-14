//@ aux-build: cov99_w350_native_raw_eq.rs
//@ compile-flags: -Zdump-mir

const MATCHED: bool = cov99_w350_native_raw_eq::equal(0x350, 0x350);

fn main() {
    assert!(MATCHED);
}
