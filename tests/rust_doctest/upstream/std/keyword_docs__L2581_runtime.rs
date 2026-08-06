// Extracted from library/std/src/keyword_docs.rs:2581
#![allow(unused)]
fn main() {
    union IntOrFloat {
        i: u32,
        f: f32,
    }
    
    let u = IntOrFloat { f: 1.0 };
    
    unsafe {
        match u {
            IntOrFloat { i: 10 } => println!("Found exactly ten!"),
            // Matching the field `f` provides an `f32`.
            IntOrFloat { f } => println!("Found f = {f} !"),
        }
    }
}
