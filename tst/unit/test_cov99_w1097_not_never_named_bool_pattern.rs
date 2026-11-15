//@ compile-only
//@ compile-flags: --emit=metadata
#![allow(unreachable_code)]

const YES: bool = true;
const NO: bool = false;

fn main() {
    let (YES | NO) = !return;
}
