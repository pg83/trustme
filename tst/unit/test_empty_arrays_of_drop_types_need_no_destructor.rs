struct Token;
impl Drop for Token { fn drop(&mut self) {} }
const EMPTY: bool = std::mem::needs_drop::<[Token; 0]>();
const NONEMPTY: bool = std::mem::needs_drop::<[Token; 2]>();
#[inline(never)]
fn query<const N: usize>() -> bool { std::mem::needs_drop::<[Token; N]>() }
fn main() {
    assert!(!EMPTY);
    assert!(NONEMPTY);
    assert!(!query::<0>());
    assert!(query::<2>());
    assert!(!std::mem::needs_drop::<([Token; 0], u8)>());
    assert!(std::mem::needs_drop::<([Token; 0], Token)>());
}
