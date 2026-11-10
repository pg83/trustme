//@ compile-fail: non-decimal float literals are not supported

fn main() {
    let _ = 0x1.2;
}
