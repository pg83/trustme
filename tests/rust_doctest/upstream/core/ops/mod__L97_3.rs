// Extracted from library/core/src/ops/mod.rs:97
#![allow(unused)]
fn main() {
    fn do_twice<F>(mut func: F)
        where F: FnMut()
    {
        func();
        func();
    }

    let mut x: usize = 1;
    {
        let add_two_to_x = || x += 2;
        do_twice(add_two_to_x);
    }

    assert_eq!(x, 5);
}
