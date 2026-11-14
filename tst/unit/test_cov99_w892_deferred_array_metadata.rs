//@ aux-build: cov99_w892_deferred_array_metadata.rs
//@ compile-flags: -Z mir-opt-level=0
//@ env: TRUSTME_DUMP=mir:hir

fn main() {
    let array = [11u8, 29, 47];
    assert_eq!(cov99_w892_deferred_array_metadata::at::<2>(&array, 1), 29);
}
