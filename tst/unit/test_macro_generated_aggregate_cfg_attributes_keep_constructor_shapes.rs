macro_rules! shape {
    ($name:ident, $ty:ty) => {
        #[derive(Debug, PartialEq)]
        struct $name(
            #[cfg_attr(all(), cfg_attr(all(), cfg(any())))] Missing,
            #[cfg_attr(all(), cfg(all()))] $ty,
        );
    };
}
mod nested { shape!(Record, u64); pub fn value() -> u64 { let Record(value) = Record(61); value } }
fn main() { shape!(Local, u16); assert_eq!(Local(67), Local(67)); assert_eq!(nested::value(), 61); }
