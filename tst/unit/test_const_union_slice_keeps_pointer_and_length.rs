union View { text: &'static str, bytes: &'static [u8] }
static DATA: [u8; 5] = [7, 11, 19, 23, 31];
const VALUE: View = View { bytes: unsafe { std::slice::from_raw_parts(DATA.as_ptr().add(1), 3) } };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        assert_eq!(value.bytes, &[11, 19, 23]);
        assert_eq!(value.bytes.as_ptr(), DATA.as_ptr().add(1));
    }
}
