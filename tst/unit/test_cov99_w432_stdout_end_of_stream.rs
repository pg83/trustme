//@ proc-macro-aux-build: cov99_w432_stdout_end_of_stream_macro.rs
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

extern crate cov99_w432_stdout_end_of_stream_macro;

cov99_w432_stdout_end_of_stream_macro::write_end_of_stream!();

fn main() {}
