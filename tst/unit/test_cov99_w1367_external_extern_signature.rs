//@ edition: 2021
//@ aux-build: cov99_w1367_delegation_types.rs
#![feature(fn_delegation)]

reuse cov99_w1367_delegation_types::identity as delegated;

fn main() {
    let pointer = std::ptr::null::<u8>() as *const cov99_w1367_delegation_types::Opaque;
    let result = delegated::<7>(pointer, cov99_w1367_delegation_types::Token::<7>);
    assert!(result == pointer);
}
