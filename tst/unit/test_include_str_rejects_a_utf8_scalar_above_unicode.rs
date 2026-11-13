//@ compile-fail: error:0: include_str! input is not valid UTF-8
//@ input: aux/invalid_utf8_scalar_above_unicode.txt

fn main() {
    let _ = include_str!("aux/invalid_utf8_scalar_above_unicode.txt");
}
