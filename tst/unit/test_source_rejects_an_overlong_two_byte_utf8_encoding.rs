//@ input: aux/invalid_utf8_overlong_two.bin
//@ compile-fail: Invalid UTF-8

#[path = "aux/invalid_utf8_overlong_two.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
