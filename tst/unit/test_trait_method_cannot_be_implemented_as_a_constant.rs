//@ compile-fail: error

trait Operations {
    fn slot();
}

struct Local;

impl Operations for Local {
    const slot: u8 = 13;
}

fn main() {}
