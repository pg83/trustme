#![allow(unreachable_code)]

const CONST_TRUE: bool = true || panic!("short-circuit OR evaluated its RHS");
const CONST_FALSE: bool = false && panic!("short-circuit AND evaluated its RHS");

fn never_on_left_of_and() -> bool {
    (return true) && false
}

fn never_on_left_of_or() -> bool {
    (return false) || true
}

fn main() {
    assert!(CONST_TRUE);
    assert!(!CONST_FALSE);
    assert!(never_on_left_of_and());
    assert!(!never_on_left_of_or());
}
