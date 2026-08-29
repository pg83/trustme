//@ check-pass

use std::io::Write;
use std::mem::ManuallyDrop;
use std::sync::Mutex;

fn main() {
    let captured = Mutex::new(vec![1_u8, 2, 3]);
    let data = captured.lock().unwrap();
    let mut output = ManuallyDrop::new(Vec::new());

    output.write_all(&data).unwrap();
    assert_eq!(&**output, &[1, 2, 3]);
}
