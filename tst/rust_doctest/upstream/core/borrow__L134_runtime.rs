// Extracted from library/core/src/borrow.rs:134
#![allow(unused)]
fn main() {
    use std::hash::{Hash, Hasher};
    pub struct CaseInsensitiveString(String);
    impl Hash for CaseInsensitiveString {
        fn hash<H: Hasher>(&self, state: &mut H) {
            for c in self.0.as_bytes() {
                c.to_ascii_lowercase().hash(state)
            }
        }
    }
}
