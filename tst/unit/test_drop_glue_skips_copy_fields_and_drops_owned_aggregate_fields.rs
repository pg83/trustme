use std::cell::Cell;
struct Mark<'a>(&'a Cell<u16>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
struct Packet<'a> {
    integer: u16,
    pointer: *const u16,
    reference: &'a u16,
    function: fn() -> u16,
    mark: Mark<'a>,
}
fn value() -> u16 { 7 }

fn main() {
    let count = Cell::new(0);
    let number = 11;
    {
        let packet = Packet {
            integer: 3, pointer: &number, reference: &number,
            function: value, mark: Mark(&count),
        };
        assert_eq!(packet.integer + *packet.reference + (packet.function)(), 21);
        assert_eq!(unsafe { *packet.pointer }, 11);
        assert_eq!(packet.mark.0.get(), 0);
        let _array = [Mark(&count), Mark(&count)];
        let _tuple = (value, Mark(&count), &number);
    }
    assert_eq!(count.get(), 4);
}
