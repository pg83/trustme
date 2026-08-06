// Extracted from src/items/generics.md:135
#![allow(unused)]
fn main() {
    struct S<const N: i64>;
    const C: i64 = 1;
    fn f<const N: i64>() -> S<N> { S }
    
    let _ = f::<1>(); // Literal.
    let _ = f::<-1>(); // Negative literal.
    let _ = f::<{ 1 + 2 }>(); // Constant expression.
    let _ = f::<C>(); // Single segment path.
    let _ = f::<{ C + 1 }>(); // Constant expression.
    let _: S<1> = f::<_>(); // Inferred const.
    let _: S<1> = f::<(((_)))>(); // Inferred const.
}
