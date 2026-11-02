//@ aux-build: coverage_path_routes.rs
//@ compile-variant: -O
use coverage_path_routes::u16;

fn main() {
    let tag: u16::Tag = u16::Tag { value: u16::LIMIT };
    let value: u16::nested::Word = u16::nested::VALUE;
    assert_eq!(match tag { u16::Tag { value } => value }, 43);
    assert_eq!(match (u16::Empty {}) { u16::Empty {} => value }, 317);
    assert_eq!(u16::MAX, 65535);
}
