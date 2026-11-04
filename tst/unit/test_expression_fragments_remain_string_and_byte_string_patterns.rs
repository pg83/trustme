//@ compile-variant: -O
macro_rules! literal_matcher {
    ($name:ident, $ty:ty, $pattern:expr) => {
        fn $name(value: $ty) -> bool {
            match value { $pattern => true, _ => false }
        }
    };
}

literal_matcher!(is_ink, &str, "ink");
literal_matcher!(is_mark, &[u8], b"mark");

fn main() {
    assert!(is_ink("ink"));
    assert!(!is_ink("inks"));
    assert!(!is_ink(""));
    assert!(is_mark(b"mark"));
    assert!(!is_mark(b"mars"));
    assert!(!is_mark(b""));
}
