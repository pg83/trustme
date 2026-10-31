//@ compile-fail: Type mismatch
enum Tag { First }
fn main() {
    let pointer = std::ptr::null();
    let _ = pointer as Tag;
}
