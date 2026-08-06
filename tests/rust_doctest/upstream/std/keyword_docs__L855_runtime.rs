// Extracted from library/std/src/keyword_docs.rs:855
#![allow(unused)]
fn main() {
    let shadowing_example = true;
    let shadowing_example = 123.4;
    let shadowing_example = shadowing_example as u32;
    let mut shadowing_example = format!("cool! {shadowing_example}");
    shadowing_example += " something else!"; // not shadowing
}
