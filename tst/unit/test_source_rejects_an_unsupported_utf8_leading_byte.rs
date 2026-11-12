//@ input: aux/invalid_utf8_unsupported_lead.bin
//@ compile-fail: Invalid UTF-8 (too long)

#[path = "aux/invalid_utf8_unsupported_lead.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, 991);
}
