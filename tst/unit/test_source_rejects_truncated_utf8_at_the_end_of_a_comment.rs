//@ input: aux/invalid_utf8_truncated_comment.bin
//@ compile-fail: UTF-8

#[path = "aux/invalid_utf8_truncated_comment.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
