//@ compile-fail: error

struct Tag;

impl Tag {
    fn key() -> u8 {
        41
    }
}

fn main() {
    match 41u8 {
        Tag::key => (),
        _ => (),
    }
}
