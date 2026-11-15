//@ crate-type: lib
//@ compile-flags: -C codegen-type=monomir
//@ output-contains: .mir = "\\"{};
#![no_std]

pub static BACKSLASH: [u8; 1] = *b"\\";
