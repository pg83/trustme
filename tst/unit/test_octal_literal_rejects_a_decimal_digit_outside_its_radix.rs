//@ compile-fail: Invalid digit in octal literal

fn main() {
    let _ = 0o98u16;
}
