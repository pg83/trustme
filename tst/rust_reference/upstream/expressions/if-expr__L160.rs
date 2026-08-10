// Extracted from src/expressions/if-expr.md:160
#![allow(unused)]
fn main() {
    fn single() {
        let outer_opt = Some(Some(1i32));
    
        if let Some(inner_opt) = outer_opt
            && let Some(number) = inner_opt
            && number == 1
        {
            println!("Peek a boo");
        }
    }
}
