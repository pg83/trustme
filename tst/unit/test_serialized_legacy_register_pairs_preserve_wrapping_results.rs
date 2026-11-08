//@ aux-build: legacy_register_pair.rs
extern crate legacy_register_pair;

fn main() {
    unsafe {
        let (carried, output) = legacy_register_pair::bump(17u16, 31);
        assert_eq!(carried, 17);
        assert_eq!(output, 38);

        let (carried, output) = legacy_register_pair::bump((5u8, 9u8), 0xffff_fffc);
        assert_eq!(carried, (5, 9));
        assert_eq!(output, 3);
    }
}
