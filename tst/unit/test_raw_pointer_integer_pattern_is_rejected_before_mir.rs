//@ compile-fail: cannot match
fn main() {
    let pointer: *const u32 = std::ptr::null();
    match pointer { 0 => (), _ => () }
}
