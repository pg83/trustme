//@ compile-fail: Found invalid character

fn main() {
    let _ = "\u{}";
}
