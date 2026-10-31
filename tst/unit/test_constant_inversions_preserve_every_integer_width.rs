fn main() {
    let a = 37u8;
    let b = 37u16;
    let c = 37u32;
    let d = 37u64;
    let e = 37u128;
    let f = 37usize;
    let g = -37i8;
    let h = -37i16;
    let i = -37i32;
    let j = -37i64;
    let k = -37i128;
    let l = -37isize;
    let yes = true;
    let no = false;
    let unsigned = [(!a) as u128, (!b) as u128, (!c) as u128, (!d) as u128, !e, (!f) as u128];
    let signed = [(!g) as i128, (!h) as i128, (!i) as i128, (!j) as i128, !k, (!l) as i128];
    let flags = (!yes, !no);
    assert_eq!(unsigned, [218, 65498, 4294967258, 18446744073709551578, u128::MAX - 37, (usize::MAX - 37) as u128]);
    assert_eq!(signed, [36; 6]);
    assert_eq!(flags, (false, true));
}
