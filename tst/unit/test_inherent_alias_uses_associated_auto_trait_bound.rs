#![feature(auto_traits, inherent_associated_types)]
auto trait Flag {}
trait Assoc { type Item: Flag; }
struct Twin<T>(T);
impl<T: Flag> Twin<T> { type Output = u16; }
type Projected<T: Assoc> = Twin<T::Item>::Output;
struct Host;
impl Assoc for Host { type Item=u8; }
fn main() { let value: Projected<Host> = 257;assert_eq!(value,257); }
