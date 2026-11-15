//@ compile-flags: -Cpanic=abort

fn must_not_run(_: u8) {
    panic!("indirect callee must not be invoked");
}

fn return_in_callee() {
    ((return, must_not_run as fn(u8)).1)(0);
}

fn return_in_argument() {
    let call: fn(u8) = must_not_run;
    call(return);
}

fn main() {
    return_in_callee();
    return_in_argument();
}
