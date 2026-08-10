// Extracted from src/macros-by-example.md:615
#![allow(unused)]
fn main() {
    macro_rules! m {
        (define) => {
            let x = 1;
        };
        (refer) => {
            dbg!(x);
        };
    }
    
    m!(define);
    m!(refer);
}
