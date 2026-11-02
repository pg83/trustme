//@ compile-variant: -O
//@ compile-variant: -O -Cpanic=abort
extern "C" { fn exit(status: i32) -> !; }

fn finish() -> ! { unsafe { exit(0) } }
fn choose(finish_now: bool) -> u32 {
    if finish_now { finish() }
    43
}

fn main() { assert_eq!(choose(std::hint::black_box(false)), 43); }
