#![feature(no_core)]
#![no_core]
#![no_std]

extern crate std;

fn main() {
    std::assert_eq!(3u16 + 4, 7);
}
