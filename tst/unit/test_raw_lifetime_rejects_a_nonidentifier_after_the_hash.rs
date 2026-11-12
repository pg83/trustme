//@ compile-fail: Invalid raw lifetime

fn take(value: &'r#- u16) {}

fn main() {}
