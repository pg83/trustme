// Extracted from src/testing/unit_testing.md:115
#![allow(unused)]
fn main() {
    pub fn divide_non_zero_result(a: u32, b: u32) -> u32 {
        if b == 0 {
            panic!("Divide-by-zero error");
        } else if a < b {
            panic!("Divide result is zero");
        }
        a / b
    }
    
    #[cfg(test)]
    mod tests {
        use super::*;
    
        #[test]
        fn test_divide() {
            assert_eq!(divide_non_zero_result(10, 2), 5);
        }
    
        #[test]
        #[should_panic]
        fn test_any_panic() {
            divide_non_zero_result(1, 0);
        }
    
        #[test]
        #[should_panic(expected = "Divide result is zero")]
        fn test_specific_panic() {
            divide_non_zero_result(1, 10);
        }
    
        #[test]
        #[should_panic = "Divide result is zero"] // This also works
        fn test_specific_panic_shorthand() {
            divide_non_zero_result(1, 10);
        }
    }
}
