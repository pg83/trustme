//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir -Z dump-hir -C panic=abort

macro_rules! shared_call {
    ($direct:expr, $name:ident, $call:expr) => {{
        if $direct {
            $call
        } else {
            let $name = $name;
            $call
        }
    }};
}

#[track_caller]
fn tracked() -> u32 {
    std::panic::Location::caller().line()
}

fn choose(direct: bool) -> u32 {
    shared_call!(direct, tracked, tracked())
}

fn main() {
    assert_eq!(choose(true), choose(false));
}
