// Extracted from library/core/src/borrow.rs:119
#![allow(unused)]
fn main() {
    pub struct CaseInsensitiveString(String);
    
    impl PartialEq for CaseInsensitiveString {
        fn eq(&self, other: &Self) -> bool {
            self.0.eq_ignore_ascii_case(&other.0)
        }
    }
    
    impl Eq for CaseInsensitiveString { }
}
