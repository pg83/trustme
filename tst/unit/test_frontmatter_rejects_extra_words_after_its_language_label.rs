--- Cargo.toml invalid
[package]
name = "extra-label-word"
---
//@ compile-fail: Unexpected token
#![feature(frontmatter)]
fn main() {}
