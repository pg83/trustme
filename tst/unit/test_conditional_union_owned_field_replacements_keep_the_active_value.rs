use std::mem::ManuallyDrop;
union Storage { text: ManuallyDrop<String>, marker: usize }

fn main() {
    let mut storage = Storage { text: ManuallyDrop::new(String::from("start")) };
    for index in 0..4 {
        let previous = unsafe { ManuallyDrop::take(&mut storage.text) };
        if std::hint::black_box(index % 2 == 0) {
            storage.text = ManuallyDrop::new(previous + "e");
        } else {
            storage.text = ManuallyDrop::new(previous + "o");
        }
    }
    let text = unsafe { ManuallyDrop::into_inner(storage.text) };
    assert_eq!(text, "starteoeo");
}
