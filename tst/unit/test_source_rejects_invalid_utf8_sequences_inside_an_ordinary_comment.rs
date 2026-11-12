//@ input: aux/invalid_utf8_comment_sequences.bin
//@ compile-fail: Invalid UTF-8

#[path = "aux/invalid_utf8_comment_sequences.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
