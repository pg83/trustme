trait Mapper {
    #[unsafe(link_section = ".text.coverage_trait_default")]
    fn map(value: u16) -> u16 {
        value ^ 0x35
    }
}

struct Marker;
impl Mapper for Marker {}

fn main() {
    assert_eq!(Marker::map(0x62), 0x57);
}
