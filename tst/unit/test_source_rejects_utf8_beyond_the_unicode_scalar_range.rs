//@ input: aux/invalid_utf8_beyond_unicode.bin
//@ compile-fail: Invalid UTF-8

#[path = "aux/invalid_utf8_beyond_unicode.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
