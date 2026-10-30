//@ compile-fail: field
struct Slots(u8, u8);
fn main() {
    let mut slots = Slots(3, 5);
    loop {
        slots.wrong = 7;
        break;
    }
}
