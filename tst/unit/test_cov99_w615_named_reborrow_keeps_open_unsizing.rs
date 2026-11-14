//@ compile-only

fn main() {
    let value = 7u8;
    let joined = if true { &value } else { &value };
    let borrowed = &joined;
    let _: &_ = borrowed;
}
