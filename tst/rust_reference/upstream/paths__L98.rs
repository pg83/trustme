// Extracted from src/paths.md:98
#![allow(unused)]
fn main() {
    mod m {
        pub const C: usize = 1;
    }
    const C: usize = m::C;
    fn f<const N: usize>() -> [u8; N] { [0; N] }
    
    let _ = f::<1>(); // Literal.
    let _: [_; 1] = f::<_>(); // Inferred const.
    let _: [_; 1] = f::<(((_)))>(); // Inferred const.
    let _ = f::<C>(); // Single segment path.
    let _ = f::<{ m::C }>(); // Multi-segment path must be braced.
}
