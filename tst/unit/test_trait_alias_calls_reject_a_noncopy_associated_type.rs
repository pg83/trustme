//@ compile-fail: Failed to find an impl of
#![feature(trait_alias)]
trait Provider { type Item; }
trait CopyFamily = Provider<Item: Copy>;
struct Text;
impl Provider for Text { type Item = String; }
fn require<T: CopyFamily>() {}
fn main() { require::<Text>(); }
