#![feature(inherent_associated_types)]
trait Assoc { type Item; }
struct Twin<T>(T);
impl<T> Twin<(T,T)> { type Output = u16; }
type Projected<T> = Twin<(<T as Assoc>::Item,<T as Assoc>::Item)>::Output;
struct Host;
impl Assoc for Host { type Item=u8; }
fn main() { let value: Projected<Host> = 257;assert_eq!(value,257); }
