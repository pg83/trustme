// Extracted from src/divergence.md:8
#![allow(unused)]
fn main() {
    fn diverges() -> ! {
        panic!("This function never returns!");
    }
    
    fn example() {
        let x: i32 = diverges(); // This line never completes.
        println!("This is never printed: {x}");
    }
}
