//@ compile-fail: `self` with no path, use `as` to give it a name
use ::{self};

fn main() {}
