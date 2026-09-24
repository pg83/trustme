// A closure's constituents for an auto trait are its captured upvars
// (upstream `closure_analyze` infers them at the end of type checking and
// the pending obligations are selected again). We had no captures during
// type checking and proved every auto trait of every closure.
//@ compile-fail: Failed to find an impl
use std::rc::Rc;

fn is_send<T: Send>(_: &T) {}

fn main() {
    let n = 5u8;
    let copies = || n + 1;
    is_send(&copies);
    let s = String::from("x");
    let t = std::thread::spawn(move || s.len());
    assert_eq!(t.join().unwrap(), 1);
    let mut pairs = (0..3usize).map(|i| i * 2).collect::<Vec<_>>();
    std::thread::spawn(move || {
        for (i, doubled) in pairs.drain(..).enumerate() {
            assert_eq!(i * 2, doubled);
        }
    })
    .join()
    .unwrap();
    let rc = Rc::new(1);
    let shares_rc = move || *rc;
    is_send(&shares_rc);
}
