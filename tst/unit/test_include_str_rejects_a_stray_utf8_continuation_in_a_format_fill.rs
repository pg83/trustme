//@ compile-fail: error:0:
//@ input: aux/invalid_utf8_format_fill.txt

fn main() {
    let _ = format!(include_str!("aux/invalid_utf8_format_fill.txt"), 7);
}
