//@ compile-fail: Expected terminating } in \u sequence

fn main() {
    let _ = "\u{0041";
}
