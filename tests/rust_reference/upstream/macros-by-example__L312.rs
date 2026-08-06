// Extracted from src/macros-by-example.md:312
#![allow(unused)]
fn main() {
    mac::m!(); // OK: Path-based lookup finds `m` in the mac module.
    
    mod mac {
        // Introduce macro `m` with textual scope.
        macro_rules! m {
            () => {};
        }
    
        // Reexport with path-based scope from within `m`'s textual scope.
        pub(crate) use m;
    }
}
