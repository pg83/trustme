//@ compile-fail: Type mismatch between u32 and u8
fn reject<F: FnOnce() -> (u8, u16)>(callback: F) {
    let result: (u32, _) = callback();
    let _ = result;
}
fn main() {}
