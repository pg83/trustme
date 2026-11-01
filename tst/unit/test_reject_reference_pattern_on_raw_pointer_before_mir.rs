//@ compile-fail: error:0:

fn main() {
    let value = 37u8;
    let pointer = &value as *const u8;
    let &inner = pointer;
    let _ = inner;
}
