// `contract_requires(|| ..)` and `contract_ensures(|ret| ..)` state a condition
// the caller and the function each have to hold to. Nothing here checks them,
// and a condition that holds changes no result.
//
// Note: Rust 1.97 has dropped `contract_requires` and rejects a contract on a
// function with no return type, both of which Rust 1.90 accepts, so only the
// form both compilers agree on is covered here.
//@ compile-flags: -Zcontract-checks=yes
#![feature(contracts_internals)]

fn ensures_positive() -> i32
    contract_ensures(|ret| *ret > 0)
{
    let inner_closure = || -> i32 { 0 };
    inner_closure();
    10
}

fn main() {
    assert_eq!(ensures_positive(), 10);
}
