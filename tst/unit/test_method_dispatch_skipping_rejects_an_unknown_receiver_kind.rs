//@ compile-fail: Unknown rustc_skip_during_method_dispatch receiver
#![feature(rustc_attrs)]
#[rustc_skip_during_method_dispatch(unknown_receiver)]
trait Traits {}
fn main() {}
