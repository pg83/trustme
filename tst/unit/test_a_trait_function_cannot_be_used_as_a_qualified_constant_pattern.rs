//@ compile-fail: error

trait Code {
    fn key() -> u8;
}

struct Tag;

impl Code for Tag {
    fn key() -> u8 {
        37
    }
}

fn main() {
    match 37u8 {
        <Tag as Code>::key => (),
        _ => (),
    }
}
