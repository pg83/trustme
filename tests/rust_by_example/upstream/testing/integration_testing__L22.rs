// Extracted from src/testing/integration_testing.md:22
#![allow(unused)]
fn main() {
    #[test]
    fn test_add() {
        assert_eq!(adder::add(3, 2), 5);
    }
}
