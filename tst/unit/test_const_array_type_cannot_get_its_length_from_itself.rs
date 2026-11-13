//@ compile-fail: error:0:

const SELF_SIZED: [u8; SELF_SIZED.len()] = [];

fn main() {}
