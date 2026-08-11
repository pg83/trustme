
#![feature(lang_items)]
fn main() {
    let lambda = |&c| c != b'9';

    let a = b'1';
    lambda(&a);
}
