#![feature(auto_traits, inherent_associated_types)]
auto trait Flag {}
struct Host;
impl Flag for Host {}
struct Twin<T>(T);
impl<T: Flag> Twin<T> { type Output = u16; }
type Projected = Twin<Host>::Output;
fn main() { let value: Projected = 257;assert_eq!(value,257); }
