//@ reject-flags: --edition=2021 --cfg overrun => Too many `super` components || too many leading `super` keywords
const ROOT: u16 = 17;
mod shelf {
    pub const VALUE: u16 = 7;
    pub fn read() -> u16 {
        const VALUE: u16 = 99;
        mod child {
            pub fn read() -> u16 {
                #[cfg(overrun)] { let _ = self::super::super::super::ROOT; }
                self::super::VALUE + super::VALUE + self::super::super::ROOT
            }
        }
        assert_eq!(VALUE, 99);
        child::read()
    }
}
fn main() { assert_eq!(shelf::read(), 31); }
