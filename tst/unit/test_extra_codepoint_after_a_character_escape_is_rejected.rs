//@ compile-fail: character literal may only contain one codepoint

fn main() {
    let _ = '\nX';
}
