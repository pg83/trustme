use std::cell::RefCell;
struct Guard<'a>(usize, &'a RefCell<Vec<usize>>);
impl Drop for Guard<'_> { fn drop(&mut self) { self.1.borrow_mut().push(self.0); } }
fn branch(mode: u8) {
    let log = RefCell::new(Vec::new());
    {
        let array: [Guard<'_>; 64] = std::array::from_fn(|i| Guard(i, &log));
        match mode {
            0 => { let [first, .., last] = array; drop(first); drop(last); }
            1 => { let [first, ..] = array; drop(first); }
            _ => { let [.., last] = array; drop(last); }
        }
    }
    let mut actual = log.into_inner();
    match mode {
        0 => assert_eq!(&actual[..2], &[0, 63]),
        1 => assert_eq!(actual[0], 0),
        _ => assert_eq!(actual[0], 63),
    }
    actual.sort();
    assert_eq!(actual, (0..64).collect::<Vec<usize>>());
}
fn conditional(initialize: bool) {
    let log = RefCell::new(Vec::new());
    {
        let array: [Guard<'_>; 64];
        if initialize {
            array = std::array::from_fn(|i| Guard(i, &log));
            let [first, ..] = array;
            drop(first);
        }
    }
    let expected: Vec<usize> = if initialize { (0..64).collect() } else { Vec::new() };
    assert_eq!(*log.borrow(), expected);
}
fn direct() {
    let log = RefCell::new(Vec::new());
    {
        let array: [Guard<'_>; 64] = std::array::from_fn(|i| Guard(i, &log));
        let [first, .., last] = array;
        drop(last);
        drop(first);
    }
    assert_eq!(log.into_inner(), [63, 0].into_iter().chain(1..63).collect::<Vec<usize>>());
}
fn main() {
    direct();
    for mode in 0..3 { branch(mode); }
    conditional(false);
    conditional(true);
}
