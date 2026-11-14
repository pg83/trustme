//@ compile-fail: Mismatched argument count (expected 1, got 0)

trait OneArgument {
    fn check(value: u8);
}

struct MissingArgument;

impl OneArgument for MissingArgument {
    fn check() {}
}

fn main() {}
