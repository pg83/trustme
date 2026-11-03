//@ compile-variant: -O
fn plain(value: u16) -> u16 { value + 1 }
unsafe fn marked(value: u16) -> u16 { value + 3 }
fn main() {
    for select_plain in [false, true] {
        let call = if std::hint::black_box(select_plain) {
            plain as fn(u16) -> u16
        } else {
            marked as unsafe fn(u16) -> u16
        };
        let reversed = if std::hint::black_box(select_plain) {
            marked as unsafe fn(u16) -> u16
        } else {
            plain as fn(u16) -> u16
        };
        assert_eq!(unsafe { call(601) }, if select_plain { 602 } else { 604 });
        assert_eq!(unsafe { reversed(607) }, if select_plain { 610 } else { 608 });
    }
}
