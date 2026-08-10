// Extracted from src/types/closure.md:327
#![allow(unused)]
fn main() {
    enum E<T> { V(T) } // A single-variant enum.
    let x = E::V(());
    let c = || {
        let E::V(_) = x; // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
}
