//@ compile-fail: long_running_const_eval
//@ compile-flags: -Dlong_running_const_eval

const NEVER_FINISHES: () = loop {};

fn main() {
    let _ = NEVER_FINISHES;
}
