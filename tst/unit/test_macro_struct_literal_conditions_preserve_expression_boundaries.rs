#[derive(Clone, Copy)]
struct Record { yes: bool, no: bool, number: u8, values: [u8; 2], pointer: &'static bool }
impl Record { fn enabled(self) -> bool { self.yes } }
macro_rules! check {
    ($record:expr) => {{
        assert!(if $record.yes { true } else { false });
        assert!(if $record.enabled() { true } else { false });
        assert!(if !$record.no { true } else { false });
        assert!(if *$record.pointer { true } else { false });
        assert!(if $record.number as u16 == 13 { true } else { false });
        assert!(if $record.values[1] == 5 { true } else { false });
        assert!(if $record.yes && !$record.no { true } else { false });
    }};
}
fn main() {
    check!(Record { yes: true, no: false, number: 13, values: [3, 5], pointer: &true });
}
