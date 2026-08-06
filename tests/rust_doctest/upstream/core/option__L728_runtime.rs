// Extracted from library/core/src/option.rs:728
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let text: Option<String> = Some("Hello, world!".to_string());
        // First, cast `Option<String>` to `Option<&String>` with `as_ref`,
        // then consume *that* with `map`, leaving `text` on the stack.
        let text_length: Option<usize> = text.as_ref().map(|s| s.len());
        println!("still can print text: {text:?}");
        Ok(())
    }
    doctest().unwrap();
}
