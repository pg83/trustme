// Extracted from library/core/src/fmt/mod.rs:50
#![allow(unused)]
fn main() {
    use std::fmt;

    #[derive(Debug)]
    struct Triangle {
        a: f32,
        b: f32,
        c: f32
    }

    impl fmt::Display for Triangle {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "({}, {}, {})", self.a, self.b, self.c)
        }
    }

    let pythagorean_triple = Triangle { a: 3.0, b: 4.0, c: 5.0 };

    assert_eq!(format!("{pythagorean_triple}"), "(3, 4, 5)");
}
