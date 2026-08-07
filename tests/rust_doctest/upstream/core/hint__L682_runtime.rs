// Extracted from library/core/src/hint.rs:682
#![allow(unused)]
#![feature(likely_unlikely)]
fn main() {
    use core::hint::unlikely;

    fn foo(x: i32) {
        if unlikely(x > 0) {
            println!("this branch is unlikely to be taken");
        } else {
            println!("this branch is likely to be taken");
        }

        match unlikely(x > 0) {
            true => println!("this branch is unlikely to be taken"),
            false => println!("this branch is likely to be taken"),
        }

        // Use outside of a branch condition may still influence a nearby branch
        let cond = unlikely(x != 0);
        if cond {
            println!("this branch is likely to be taken");
        }
    }
}
