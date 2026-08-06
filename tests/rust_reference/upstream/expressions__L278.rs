// Extracted from src/expressions.md:278
#![allow(unused)]
fn main() {
    fn example() -> Result<(), impl std::fmt::Debug> {
        let x = {
            // This call creates an internal temporary.
            let x = format_args!("{:?}", 0);
            x // <-- The temporary is extended, allowing its use here.
        }; // <-- The temporary is dropped here.
        x; // ERROR
        Ok(())
    }
    example().unwrap();
}
