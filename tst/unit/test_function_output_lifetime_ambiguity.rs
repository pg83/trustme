//@ compile-fail: Unspecified lifetime in outer context

fn ambiguous<'a>(left: &'a u8, _right: &'a u8) -> &u8 {
    left
}

fn main() {}
