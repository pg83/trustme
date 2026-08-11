// { dg-skip-if "" { *-*-* } { "-m32" } { "" } }
// { dg-options "-w" }
// { dg-output "Hash: 0x63d53fd2170bbb8c\r*\n" }
#![allow(deprecated)]

use std::hash::{Hasher, SipHasher};

fn gccrs_main() -> i32 {
    let mut hasher = SipHasher::new_with_keys(
        0x0706050403020100,
        0x0f0e0d0c0b0a0908,
    );
    hasher.write(b"Hello");
    println!("Hash: 0x{:016x}", hasher.finish());
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
