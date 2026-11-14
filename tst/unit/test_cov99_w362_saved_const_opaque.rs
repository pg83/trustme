//@ aux-build: cov99_w362_opaque_aux.rs
//@ compile-flags: -Zmir-opt-level=0
//@ env: TRUSTME_DUMP=mir

fn main() {
    let value = cov99_w362_opaque_aux::retained(false);
    assert_eq!(core::mem::size_of_val(&value), 0);
    drop(value);
    assert_eq!(cov99_w362_opaque_aux::LEN, 1);
}
