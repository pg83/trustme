#![feature(log_syntax, trace_macros)]

trace_macros!(false);
log_syntax!();

fn main() {
    trace_macros!(false);
    log_syntax!();

    let _ = (trace_macros!(false), log_syntax!());
}
