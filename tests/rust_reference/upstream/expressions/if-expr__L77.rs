// Extracted from src/expressions/if-expr.md:77
#![allow(unused)]
fn main() {
    fn diverging_condition() -> ! {
        // Diverges because the condition expression diverges
        if loop {} {
            ()
        } else {
            ()
        };
        // The semicolon above is important: The type of the `if` expression is
        // `()`, despite being diverging. When the final body expression is
        // elided, the type of the body is inferred to ! because the function body
        // diverges. Without the semicolon, the `if` would be the tail expression
        // with type `()`, which would fail to match the return type `!`.
    }
    
    fn diverging_arms() -> ! {
        // Diverges because all arms diverge
        if true {
            loop {}
        } else {
            loop {}
        }
    }
}
