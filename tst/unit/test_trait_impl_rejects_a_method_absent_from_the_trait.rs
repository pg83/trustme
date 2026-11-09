//@ compile-fail: error

trait Operations {}
struct Local;

impl Operations for Local {
    fn missing() {}
}

fn main() {}
