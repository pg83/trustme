trait Read { fn read(&self, extra: u16) -> u16; }
struct Number(u16);
impl Read for Number { fn read(&self, extra: u16) -> u16 { self.0 + extra } }
union View { object: &'static dyn Read, words: [usize; 2] }
static DATA: Number = Number(257);
const VALUE: View = View { object: &DATA };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!(value.object.read(9), 266); }
}
