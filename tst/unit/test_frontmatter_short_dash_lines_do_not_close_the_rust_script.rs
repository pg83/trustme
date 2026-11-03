--- Cargo.toml
[package]
name = "short-dash-lines"
description = """
--
-
"""
---
#![feature(frontmatter)]
fn main() {
    let values = [59u16, 61, 79];
    assert_eq!(values.iter().copied().sum::<u16>(), 199);
}
