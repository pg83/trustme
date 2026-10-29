//@ aux-build: root_alias_macros.rs
use crate as local_root;
use ::root_alias_macros as external_root;
use local_root as local_again;
use external_root as external_again;
#[macro_export]
macro_rules! double { ($n:expr) => { $n * 2u32 }; }
mod nested {
    use crate as here;
    use ::root_alias_macros as there;
    pub fn value() -> u32 { here::double!(there::offset!(3)) }
}
fn main() {
    assert_eq!(local_again::double!(5), 10);
    assert_eq!(external_again::offset!(7), 24);
    assert_eq!(nested::value(), 40);
}
