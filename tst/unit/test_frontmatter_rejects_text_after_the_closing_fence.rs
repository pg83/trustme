--- Cargo.toml
[package]
name = "trailing-close-text"
--- invalid
//@ compile-fail: Unexpected token
#![feature(frontmatter)]
fn main() {}
