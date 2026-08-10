// Extracted from src/paths.md:255
struct S;
type Ty = S::self; // ERROR: Structs cannot be parents of `self`.
fn main() {}
