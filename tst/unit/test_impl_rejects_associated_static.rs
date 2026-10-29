//@ compile-fail: associated static items are not allowed
struct Invalid;
impl Invalid {
    static VALUE: u8 = 3;
}
fn main() {}
