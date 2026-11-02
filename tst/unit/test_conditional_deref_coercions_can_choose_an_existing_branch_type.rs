//@ compile-variant: -O
use std::ops::Deref;
struct Target(u16);
struct Wrapper(Target);
impl Deref for Wrapper {
    type Target = Target;
    fn deref(&self) -> &Target { &self.0 }
}

fn main() {
    let wrapped = Wrapper(Target(17));
    let plain = Target(23);
    for first in [true, false] {
        let forward = if first { &wrapped } else { &plain };
        let reverse = if first { &plain } else { &wrapped };
        assert_eq!(forward.0, if first { 17 } else { 23 });
        assert_eq!(reverse.0, if first { 23 } else { 17 });
    }
}
