// A digit after the dot is a tuple index however it is spaced, but a name is a
// field or a method only when it is written right after the dot: `1.min(2)` is
// a call, while `1. as u16` is a float and a cast. A match arm's `=>` ends the
// guard, so `if let true = return =>` has no value after the `return`.
#![feature(if_let_guard)]

fn main() {
    const A: u16 = -1. as u16;
    const B: u128 = -100. as u128;
    const C: i8 = f32::NAN as i8;
    assert_eq!(A, 0);
    assert_eq!(B, 0);
    assert_eq!(C, 0);

    let b = 2.;
    assert_eq!(b, 2.0f64);
    assert_eq!(1.max(2), 2);
    assert_eq!((1.0f64).max(2.0), 2.0);
    assert_eq!(3. as u8, 3);

    let r = 1..3;
    assert_eq!(r.len(), 2);
    let r2 = 1..=3;
    assert_eq!(r2.count(), 3);

    let pair = (1u8, 2u16);
    assert_eq!(pair. 0, 1);
    assert_eq!(pair.1, 2);

    guarded();
}

fn guarded() {
    match 2 {
        x if (return) => {
            let _ = x;
        }
        x if let true = return => {
            let _ = x;
        }
        _ => {}
    }
}
