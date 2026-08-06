// Extracted from src/destructors.md:620
#![allow(unused)]
fn main() {
    fn example() -> Result<(), impl std::fmt::Debug> {
        fn temp() {}
        // As above.
        format_args!("{:?}", { &temp() }); // ERROR
        Ok(())
    }
    example().unwrap();
}
