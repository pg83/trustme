//@ compile-fail: associated static items are not allowed
trait Invalid {
    static VALUE: u8;;
}
fn main() {}
