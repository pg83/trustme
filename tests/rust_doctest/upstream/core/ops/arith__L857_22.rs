// Extracted from library/core/src/ops/arith.rs:857
#![allow(unused)]
fn main() {
    use std::ops::MulAssign;
    
    #[derive(Debug, PartialEq)]
    struct Frequency { hertz: f64 }
    
    impl MulAssign<f64> for Frequency {
        fn mul_assign(&mut self, rhs: f64) {
            self.hertz *= rhs;
        }
    }
    
    let mut frequency = Frequency { hertz: 50.0 };
    frequency *= 4.0;
    assert_eq!(Frequency { hertz: 200.0 }, frequency);
}
