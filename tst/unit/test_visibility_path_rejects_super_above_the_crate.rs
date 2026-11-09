//@ compile-fail: Too many `super`

pub(in crate::super::super) struct Hidden;

fn main() {}
