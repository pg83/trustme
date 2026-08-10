// Extracted from src/drop-flags.md:62
#![allow(unused)]
fn main() {
    let condition = true;
    let x;
    if condition {
        x = Box::new(0);        // x was uninit; just overwrite.
        println!("{}", x);
    }
                                // x goes out of scope; x might be uninit;
                                // check the flag!
}
