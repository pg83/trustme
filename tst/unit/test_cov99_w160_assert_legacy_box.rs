//@ edition: 2021

// Parser-only legacy syntax: assert! prints the AST before cfg removes the binding.
fn main() {
    assert!({
        #[cfg(any())]
        let _ = box 1u8;
        true
    });
}
