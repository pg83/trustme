fn main() {
    let f = &[|| 19u32][0];
    assert_eq!(f(), 19);
    let pair = &([|| 23u32], [|| 29u32]);
    assert_eq!(pair.0[0]() + pair.1[0](), 52);
    const VALUE: u32 = 31;
    let with_constant = &[|| VALUE][0];
    assert_eq!(with_constant(), 31);
}
