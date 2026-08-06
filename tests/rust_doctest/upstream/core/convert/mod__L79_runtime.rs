// Extracted from library/core/src/convert/mod.rs:79
#![allow(unused)]
fn main() {
    use std::convert::identity;
    
    let condition = true;
    
    fn manipulation(x: u32) -> u32 { x + 1 }
    
    let do_stuff = if condition { manipulation } else { identity };
    
    // Do more interesting stuff...
    
    let _results = do_stuff(42);
}
