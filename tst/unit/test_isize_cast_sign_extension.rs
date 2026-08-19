//@ run-pass
// Folding a cast to `isize` sign-extends the value the way a cast to `i64`
// does. It used to keep the unsigned bits, so a later signed division on the
// folded constant read a huge positive number where the value is negative.

fn main() {
    const A: isize = 0xfedc_ba98_7654_3217_u64 as isize;
    let a = 0xfedc_ba98_7654_3217_u64 as isize;

    assert!(A < 0);
    assert_eq!(A, a);
    assert_eq!(A / -2, a / -2);
    assert_eq!(A / 2, a / 2);
    assert_eq!(A % -3, a % -3);
    assert_eq!(A >> 8, a >> 8);
    assert_eq!((-1_i64 as usize as isize), -1_isize);
    assert_eq!((u64::MAX as isize), -1_isize);
    assert_eq!(((1_u64 << 63) as isize), isize::MIN);
}
