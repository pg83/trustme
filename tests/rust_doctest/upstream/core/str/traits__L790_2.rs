// Extracted from library/core/src/str/traits.rs:790
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::str::FromStr;
        
        #[derive(Debug, PartialEq)]
        struct Point {
            x: i32,
            y: i32
        }
        
        #[derive(Debug, PartialEq, Eq)]
        struct ParsePointError;
        
        impl FromStr for Point {
            type Err = ParsePointError;
        
            fn from_str(s: &str) -> Result<Self, Self::Err> {
                let (x, y) = s
                    .strip_prefix('(')
                    .and_then(|s| s.strip_suffix(')'))
                    .and_then(|s| s.split_once(','))
                    .ok_or(ParsePointError)?;
        
                let x_fromstr = x.parse::<i32>().map_err(|_| ParsePointError)?;
                let y_fromstr = y.parse::<i32>().map_err(|_| ParsePointError)?;
        
                Ok(Point { x: x_fromstr, y: y_fromstr })
            }
        }
        
        let expected = Ok(Point { x: 1, y: 2 });
        // Explicit call
        assert_eq!(Point::from_str("(1,2)"), expected);
        // Implicit calls, through parse
        assert_eq!("(1,2)".parse(), expected);
        assert_eq!("(1,2)".parse::<Point>(), expected);
        // Invalid input string
        assert!(Point::from_str("(1 2)").is_err());
        Ok(())
    }
    doctest().unwrap();
}
