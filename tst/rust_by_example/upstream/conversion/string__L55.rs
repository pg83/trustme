// Extracted from src/conversion/string.md:55
use std::num::ParseIntError;
use std::str::FromStr;

#[derive(Debug)]
struct Circle {
    radius: i32,
}

impl FromStr for Circle {
    type Err = ParseIntError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.trim().parse() {
            Ok(num) => Ok(Circle{ radius: num }),
            Err(e) => Err(e),
        }
    }
}

fn main() {
    let radius = "    3 ";
    let circle: Circle = radius.parse().unwrap();
    println!("{:?}", circle);
}
