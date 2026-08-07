// Extracted from library/core/src/primitive_docs.rs:35
#![allow(unused)]
fn main() {
    let praise_the_borrow_checker = true;

    // using the `if` conditional
    if praise_the_borrow_checker {
        println!("oh, yeah!");
    } else {
        println!("what?!!");
    }

    // ... or, a match pattern
    match praise_the_borrow_checker {
        true => println!("keep praising!"),
        false => println!("you should praise!"),
    }
}
