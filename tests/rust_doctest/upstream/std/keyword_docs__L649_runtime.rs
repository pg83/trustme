// Extracted from library/std/src/keyword_docs.rs:649
#![allow(unused)]
fn main() {
    if let Some(x) = Some(123) {
        // code
        let _ = x;
    } else {
        // something else
    }

    match Some(123) {
        Some(x) => {
            // code
            let _ = x;
        },
        _ => {
            // something else
        },
    }
}
