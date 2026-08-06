// Extracted from src/drop-flags.md:47
#![allow(unused)]
fn main() {
    let condition = true;
    let mut x = Box::new(0);    // x was uninit; just overwrite.
    if condition {
        drop(x)                 // x gets moved out; make x uninit.
    } else {
        println!("{}", x);
        drop(x)                 // x gets moved out; make x uninit.
    }
    x = Box::new(0);            // x was uninit; just overwrite.
                                // x goes out of scope; x was init; Drop x!
}
