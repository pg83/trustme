// semver-parser's `OneOrMore(OneByte(|c| c == b'-' || ..)).p(s)` borrows a
// constant expression holding a closure without captures, which upstream
// promotes like any other constant aggregate. Deciding whether to promote
// it, we asked for the layout of a type that still held the closure itself
// (closures become structs only in a later pass), and the name of that type
// could not be mangled.
pub trait Recognize {
    fn p(&self, s: &[u8]) -> Option<usize>;
}

pub struct OneByte<F>(pub F);

impl<F: Fn(u8) -> bool> Recognize for OneByte<F> {
    fn p(&self, s: &[u8]) -> Option<usize> {
        if s.is_empty() || !self.0(s[0]) { None } else { Some(1) }
    }
}

pub struct OneOrMore<P>(pub P);

impl<P: Recognize> Recognize for OneOrMore<P> {
    fn p(&self, s: &[u8]) -> Option<usize> {
        let mut n = 0;
        while let Some(len) = self.0.p(&s[n..]) {
            n += len;
        }
        if n == 0 { None } else { Some(n) }
    }
}

pub fn letters(s: &[u8]) -> Option<usize> {
    OneOrMore(OneByte(|c| c == b'-' || (b'a' <= c && c <= b'z'))).p(s)
}

fn main() {
    assert_eq!(letters(b"ab-c1"), Some(4));
    assert_eq!(letters(b"1"), None);
}
