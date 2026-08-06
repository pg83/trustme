// Extracted from library/core/src/ops/arith.rs:369
#![allow(unused)]
fn main() {
    use std::ops::Div;
    
    // By the fundamental theorem of arithmetic, rational numbers in lowest
    // terms are unique. So, by keeping `Rational`s in reduced form, we can
    // derive `Eq` and `PartialEq`.
    #[derive(Debug, Eq, PartialEq)]
    struct Rational {
        numerator: usize,
        denominator: usize,
    }
    
    impl Rational {
        fn new(numerator: usize, denominator: usize) -> Self {
            if denominator == 0 {
                panic!("Zero is an invalid denominator!");
            }
    
            // Reduce to lowest terms by dividing by the greatest common
            // divisor.
            let gcd = gcd(numerator, denominator);
            Self {
                numerator: numerator / gcd,
                denominator: denominator / gcd,
            }
        }
    }
    
    impl Div for Rational {
        // The division of rational numbers is a closed operation.
        type Output = Self;
    
        fn div(self, rhs: Self) -> Self::Output {
            if rhs.numerator == 0 {
                panic!("Cannot divide by zero-valued `Rational`!");
            }
    
            let numerator = self.numerator * rhs.denominator;
            let denominator = self.denominator * rhs.numerator;
            Self::new(numerator, denominator)
        }
    }
    
    // Euclid's two-thousand-year-old algorithm for finding the greatest common
    // divisor.
    fn gcd(x: usize, y: usize) -> usize {
        let mut x = x;
        let mut y = y;
        while y != 0 {
            let t = y;
            y = x % y;
            x = t;
        }
        x
    }
    
    assert_eq!(Rational::new(1, 2), Rational::new(2, 4));
    assert_eq!(Rational::new(1, 2) / Rational::new(3, 4),
               Rational::new(2, 3));
}
