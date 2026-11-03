--- Cargo.toml
[package]
name = "unclosed"
--
//@ compile-fail: Unexpected token
#![feature(frontmatter)]
fn main() {}
