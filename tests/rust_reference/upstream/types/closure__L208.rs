// Extracted from src/types/closure.md:208
#![allow(unused)]
fn main() {
    struct S; // A non-`Copy` type.
    
    // Destructuring tuples does not cause a read or capture.
    let x = (S,);
    let c = || {
        let (..) = x; // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
    
    // Destructuring unit structs does not cause a read or capture.
    let x = S;
    let c = || {
        let S = x; // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
    
    // Destructuring structs does not cause a read or capture.
    struct W<T>(T);
    let x = W(S);
    let c = || {
        let W(..) = x; // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
    
    // Destructuring single-variant enums does not cause a read
    // or capture.
    enum E<T> { V(T) }
    let x = E::V(S);
    let c = || {
        let E::V(..) = x; // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
}
