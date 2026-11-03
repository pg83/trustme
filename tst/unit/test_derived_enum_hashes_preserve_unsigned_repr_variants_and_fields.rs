#![feature(repr128)]
use std::hash::{Hash, Hasher};

struct Recorded(Vec<u8>);
impl Hasher for Recorded {
    fn finish(&self) -> u64 { 0 }
    fn write(&mut self, bytes: &[u8]) { self.0.extend_from_slice(bytes); }
}
fn bytes(value: &impl Hash) -> Vec<u8> {
    let mut recorded = Recorded(Vec::new());
    value.hash(&mut recorded);
    recorded.0
}
macro_rules! check {
    ($repr:ident, $name:ident) => {{
        #[repr($repr)]
        #[derive(Hash, PartialEq, Eq)]
        enum $name { Idle, Tuple(u16), Record { number: u16, enabled: bool } }
        assert_eq!(bytes(&$name::Tuple(257)), bytes(&$name::Tuple(257)));
        assert_ne!(bytes(&$name::Idle), bytes(&$name::Tuple(0)));
        assert_ne!(bytes(&$name::Tuple(257)), bytes(&$name::Tuple(258)));
        assert_ne!(bytes(&$name::Tuple(257)), bytes(&$name::Record { number: 257, enabled: true }));
        assert_ne!(bytes(&$name::Record { number: 257, enabled: true }), bytes(&$name::Record { number: 257, enabled: false }));
        assert!(std::mem::size_of::<$name>() >= std::mem::size_of::<$repr>());
    }};
}
fn main() {
    check!(u32, Word);
    check!(u64, Wide);
    check!(u128, Double);
    check!(usize, Native);
}
