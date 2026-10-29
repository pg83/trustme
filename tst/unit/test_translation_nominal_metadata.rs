//@ aux-build: translation_nominals.rs
use translation_nominals::{deferred, passthrough, read, Foreign, Pick, Wrapped};

fn main() {
    assert_eq!(read(&Wrapped(40)), 42);
    let make = deferred([3u16, 5, 8]);
    match make() {
        Pick::Full(Wrapped(values)) => assert_eq!(values, [3, 5, 8]),
        Pick::Empty => panic!("lost captured value"),
    }
    let byte = 7u8;
    let foreign = passthrough(&byte as *const u8 as *const Foreign);
    assert_eq!(foreign as *const u8, &byte as *const u8);
    let empty = passthrough(0usize as *const Foreign);
    assert_eq!(empty as usize, 0);
}
