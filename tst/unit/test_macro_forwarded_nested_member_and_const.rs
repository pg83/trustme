macro_rules! member { ($item:item) => { $item }; }
struct Host;
impl Host {
    member!(const BASE: u16 = 257;);
    member!(member!(fn value() -> u16 { Self::BASE + 13 }););
}
fn main() { assert_eq!(Host::value(),270); }
