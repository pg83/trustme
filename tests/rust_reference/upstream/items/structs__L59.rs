// Extracted from src/items/structs.md:59
#![allow(unused)]
fn main() {
    struct Cookie {}
    const Cookie: Cookie = Cookie {};
    let c = [Cookie, Cookie {}, Cookie, Cookie {}];
}
