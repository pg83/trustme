// tokio's fs tests write `assert_eq!(&buf[..n], &HELLO[..n])`, which
// borrows `&HELLO[..n]` again. That inner borrow reaches through the
// constant's reference and needs no promotion, but its value depends on
// the local `n`: it is no constant, and the outer borrow of it is a runtime
// temporary. Promoting the outer borrow carried `n` into a constant.
const HELLO: &[u8] = b"hello";

fn main() {
    let n = 4usize;
    let twice = &&HELLO[..n];
    assert_eq!(twice.len(), 4);
    assert_eq!(&HELLO[..n], b"hell");
    let one = &&HELLO[n];
    assert_eq!(**one, b'o');
}
