//@ input: aux/invalid_utf8_overlong_four.bin
//@ compile-fail: Invalid UTF-8

#[path = "aux/invalid_utf8_overlong_four.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
