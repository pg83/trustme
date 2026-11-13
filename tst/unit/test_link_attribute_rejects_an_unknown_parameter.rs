//@ compile-fail: Unknown attribute

#[link(name = "unused_invalid_link_parameter", unrecognized_parameter = "value")]
extern "C" {}

fn main() {}
