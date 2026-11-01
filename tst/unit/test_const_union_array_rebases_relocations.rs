#[derive(Clone, Copy)]
union Reference { byte: &'static u8, word: &'static u16 }
static LEFT: u8 = 17;
static RIGHT: u16 = 513;
const VALUES: [Reference; 2] = [Reference { byte: &LEFT }, Reference { word: &RIGHT }];

fn main() {
    let values = std::hint::black_box(VALUES);
    unsafe {
        assert_eq!(*values[0].byte, 17);
        assert_eq!(*values[1].word, 513);
        assert_eq!(values[1].word, &RIGHT);
    }
}
