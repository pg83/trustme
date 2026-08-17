// A labelled block runs once, so one that just runs off its end has to leave --
// it is not a loop. A block whose last statement already leaves it keeps the
// label's own break type.
fn assign(a: bool, b: bool) -> u32 {
    let mut v = 0;
    'b: {
        v = 1;
        if a {
            break 'b;
        }
        v = 2;
        if b {
            break 'b;
        }
        v = 3;
    }
    v
}

#[allow(unused_labels)]
fn only_breaks() -> u8 {
    'a: {
        'b: {
            break 'b 1u8;
        };
        0
    }
}

fn main() {
    assert_eq!(assign(true, false), 1);
    assert_eq!(assign(false, true), 2);
    assert_eq!(assign(false, false), 3);
    assert_eq!(only_breaks(), 0);
}
