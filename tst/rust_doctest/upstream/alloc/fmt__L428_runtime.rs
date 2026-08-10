// Extracted from library/alloc/src/fmt.rs:428
use std::fmt;

#[derive(Debug)]
struct Vector2D {
    x: isize,
    y: isize,
}

impl fmt::Display for Vector2D {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // The `f` value implements the `Write` trait, which is what the
        // write! macro is expecting. Note that this formatting ignores the
        // various flags provided to format strings.
        write!(f, "({}, {})", self.x, self.y)
    }
}

// Different traits allow different forms of output of a type. The meaning
// of this format is to print the magnitude of a vector.
impl fmt::Binary for Vector2D {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let magnitude = (self.x * self.x + self.y * self.y) as f64;
        let magnitude = magnitude.sqrt();

        // Respect the formatting flags by using the helper method
        // `pad_integral` on the Formatter object. See the method
        // documentation for details, and the function `pad` can be used
        // to pad strings.
        let decimals = f.precision().unwrap_or(3);
        let string = format!("{magnitude:.decimals$}");
        f.pad_integral(true, "", &string)
    }
}

fn main() {
    let myvector = Vector2D { x: 3, y: 4 };

    println!("{myvector}");       // => "(3, 4)"
    println!("{myvector:?}");     // => "Vector2D {x: 3, y:4}"
    println!("{myvector:10.3b}"); // => "     5.000"
}
