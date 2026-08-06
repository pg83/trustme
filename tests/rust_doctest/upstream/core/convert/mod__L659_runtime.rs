// Extracted from library/core/src/convert/mod.rs:659
#![allow(unused)]
fn main() {
    struct GreaterThanZero(i32);
    
    impl TryFrom<i32> for GreaterThanZero {
        type Error = &'static str;
    
        fn try_from(value: i32) -> Result<Self, Self::Error> {
            if value <= 0 {
                Err("GreaterThanZero only accepts values greater than zero!")
            } else {
                Ok(GreaterThanZero(value))
            }
        }
    }
}
