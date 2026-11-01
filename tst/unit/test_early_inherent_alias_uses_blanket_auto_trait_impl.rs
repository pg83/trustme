#![feature(auto_traits, inherent_associated_types)]
auto trait Flag {}
impl<T> Flag for T {}
struct Twin<T>(T);
impl<T: Flag> Twin<T> { type Output = u16; }
type Projected = Twin<u8>::Output;
fn main() { let value: Projected = 257;assert_eq!(value,257); }
