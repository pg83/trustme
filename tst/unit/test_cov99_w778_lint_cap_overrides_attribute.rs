//@ edition: 2021
//@ compile-only
//@ compile-flags: --cap-lints allow --emit=metadata

#[deny(dead_code)]
fn capped_unused() {}

fn main() {}
