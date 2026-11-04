//@ compile-fail: error
#[cfg(orchard_unknown())]
fn selected() {}
fn main() {}
