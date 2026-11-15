#![feature(decl_macro)]

fn main() {
    assert!({
        macro local() { () }
        true
    });
}
