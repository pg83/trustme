// qrcode's `interleave(&[&b"1234"[..], b"5678", b"abcdef", b"ghijkl"])` with
// `fn interleave<T: Copy, V: Deref<Target = [T]>>(blocks: &[V])`: every
// element is coerced to the element type `V`, which is still open while
// `V: Deref<Target = [u8]>` is proven. The candidate `impl Deref for &U`
// binds `U = [u8]` through its `Target`, and the element coercions are to
// be read against that: `&[u8; 4]` reaches `&[u8]` by unsizing. Read against
// a bare `&U` they were taken as `U = [u8; 4]`, and the first element's
// `&[u8]` then mismatched.
use std::ops::Deref;
fn interleave<T: Copy, V: Deref<Target = [T]>>(blocks: &[V]) -> Vec<T> {
    let mut res = Vec::new();
    for t in blocks {
        res.push(t[0]);
    }
    res
}
fn main() {
    let res = interleave(&[&b"1234"[..], b"5678", b"abcdef", b"ghijkl"]);
    assert_eq!(&*res, b"15ag");
}
