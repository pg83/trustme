// Extracted from src/testing/dev_dependencies.md:20
#![allow(unused)]
fn main() {
    pub fn add(a: i32, b: i32) -> i32 {
        a + b
    }
    
    #[cfg(test)]
    mod tests {
        use super::*;
        use pretty_assertions::assert_eq; // crate for test-only use. Cannot be used in non-test code.
    
        #[test]
        fn test_add() {
            assert_eq!(add(2, 3), 5);
        }
    }
}
