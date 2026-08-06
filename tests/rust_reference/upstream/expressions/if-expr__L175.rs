// Extracted from src/expressions/if-expr.md:175
#![allow(unused)]
fn main() {
    fn nested() {
        let outer_opt = Some(Some(1i32));
    
        if let Some(inner_opt) = outer_opt {
            if let Some(number) = inner_opt {
                if number == 1 {
                    println!("Peek a boo");
                }
            }
        }
    }
}
