//@ compile-fail: error:0:

fn main() {
    let (left, right) = 37u8;
    let _ = (left, right);
}
