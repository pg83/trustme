//@ edition: 2018
macro_rules! byte_map {
    ($($p:pat)|+) => {{
        const fn make_map() -> [bool; 256] {
            let mut ret = [false; 256];
            let mut i = 0;
            while i < 256 {
                ret[i] = matches!(i as u8, $($p)|+);
                i += 1;
            }
            ret
        }
        make_map()
    }};
}

macro_rules! either {
    ($a:pat, $b:pat) => {
        |x: u8| match x {
            $a | $b => true,
            _ => false,
        }
    };
}

static URI_MAP: [bool; 256] = byte_map!(b'!'..=0x7e | 0x80..=0xFF);

fn main() {
    assert!(URI_MAP[b'!' as usize]);
    assert!(!URI_MAP[b' ' as usize]);
    assert!(!URI_MAP[0x7f]);
    assert!(URI_MAP[0xFF]);
    let f = either!(1, 3);
    assert!(f(1) && f(3) && !f(2));
}
