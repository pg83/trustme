//@ compile-fail: Multiple data traits in trait object

fn rejected(_: &(dyn std::fmt::Display + std::fmt::Debug)) {}

fn main() {}
