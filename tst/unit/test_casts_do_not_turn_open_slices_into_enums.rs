//@ compile-fail: Type mismatch
enum Tag { First }
fn main() {
    let slice: &[_] = &[];
    let _ = slice as Tag;
}
