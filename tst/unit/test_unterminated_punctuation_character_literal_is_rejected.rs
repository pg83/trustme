//@ compile-fail: unterminated character literal

fn main() {
    let _ = '+;
}
