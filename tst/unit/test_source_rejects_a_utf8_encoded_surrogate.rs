//@ input: aux/invalid_utf8_surrogate.bin
//@ compile-fail: Invalid UTF-8

#[path = "aux/invalid_utf8_surrogate.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
