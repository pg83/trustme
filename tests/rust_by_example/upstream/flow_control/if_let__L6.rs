// Extracted from src/flow_control/if_let.md:6
#![allow(unused)]
fn main() {
    // Make `optional` of type `Option<i32>`
    let optional = Some(7);
    
    match optional {
        Some(i) => println!("This is a really long string and `{:?}`", i),
        _ => {},
        // ^ Required because `match` is exhaustive. Doesn't it seem
        // like wasted space?
    };
}
