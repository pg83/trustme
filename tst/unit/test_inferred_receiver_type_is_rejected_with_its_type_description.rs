//@ compile-fail: Unknown receiver type

struct Value;

impl Value {
    fn read(self: _) -> u16 {
        101
    }
}

fn main() {}
