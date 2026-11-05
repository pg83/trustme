//@ compile-fail: Unicode escape must start with {

fn main() {
    let _ = "\u0041";
}
