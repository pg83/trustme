// Mixing a byte-string literal and array patterns over &[u8; 4] used to abort
// match lowering (field-path mismatch: .*.0 vs .*).
fn f(x: &[u8; 4]) -> u32 {
    match x {
        b"DXT1" => 1,
        [0, 0, 0, 0] => 2,
        _ => 0,
    }
}
fn main() {
    assert_eq!(f(b"DXT1"), 1);
    assert_eq!(f(&[0, 0, 0, 0]), 2);
    assert_eq!(f(b"zzzz"), 0);
}
